// The symbols an `AstActions` names: a node factory and a conversion, each
// templated on the strict minimum, so the code behind the table's function
// pointers is shared by every grammar in the program.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cursiv/grammar/Conversion.hpp"
#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/shared/runtime/AstActions.hpp"
#include "cursiv/shared/runtime/AstBuildCtx.hpp"
#include "cursiv/shared/runtime/CancellationPoll.hpp"
#include "cursiv/shared/runtime/SmallRun.hpp"
#include "cursiv/shared/runtime/ast_conv.hpp"
#include "cursiv/shared/table/StampLayout.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"
#include "cursiv/syntax-tree/AstArena.hpp"
#include "cursiv/syntax-tree/AstField.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/utils/macros.hpp"

namespace cursiv::engine {

// One node factory per AST type. The node goes on the list at once, and the
// result destroys it.
template <class T>
AstNode* make_node(AstBuildCtx& ctx, NodeId id) {
  AstNode* const base = ctx.arena->template make<T>();
  base->cstNodeId = id;
  ctx.list(base);
  return base;
}

using PartRun = SmallRun<std::string_view, 8>;

// Whether a record's text conversions may run: the grammar converts text below
// it, or a repair left leaves that the static analysis does not cover.
template <bool Text>
bool converts_text(const AstBuildCtx& ctx) {
  return Text || ctx.repaired;
}

// A token a repair dropped: a recovered leaf with neither a row nor a state.
// It holds no value and invents nothing, even with no width.
inline bool dropped_token(const AstBuildCtx& ctx, const CstNode& n) {
  return n.recovered && n.subtreeSize == 1 && ctx.stamp.row(n) == kNoElement &&
         ctx.stamp.state(n) == 0;
}

// A leaf that gives its parent a part: read text, or a misspelt word re-read.
// A token with no width gives none; a call that read nothing gives one.
inline bool part_leaf(const AstBuildCtx& ctx, const CstNode& n) {
  if (n.hidden) return false;
  if (n.recovered) return n.begin != n.end && ctx.stamp.state(n) != 0;
  if (n.begin != n.end) return true;
  const ElementId row = ctx.stamp.row(n);
  return ctx.actions->is_call(row) || ctx.actions->call_at(row) != nullptr;
}

// The parts in one text, a view of the source where they are adjacent. Only a
// run with no part gives the null view, which marks a member never written.
inline std::string_view join_parts(AstBuildCtx& ctx, const PartRun& run) {
  const char* first = nullptr;
  const char* end = nullptr;
  bool verbatim = true;
  for (const std::string_view part : run) {
    if (part.empty()) continue;
    if (first == nullptr)
      first = part.data();
    else if (part.data() != end)
      verbatim = false;
    end = part.data() + part.size();
  }
  if (first == nullptr) {
    for (const std::string_view part : run)
      if (part.data() != nullptr) return part;
    return std::string_view{};
  }
  if (verbatim)
    return std::string_view(first, static_cast<std::size_t>(end - first));
  return ctx.keep_text(
      std::span<const std::string_view>(run.begin(), run.size()));
}

// Whether a converter is handed what it converts, its parts or its text; one
// that is not reads nothing.
template <class Cv>
inline constexpr bool converts_input = ConvertsParts<Cv> || ConvertsText<Cv>;

struct TextValues {
  // A converter that failed is never recorded: the evaluation stops there.
  struct Value {
    TextFn fn;
    std::string_view text;
  };
  std::unordered_map<NodeId, Value> calls;
};

// The parts below `id`, in source order, a repaired wrapper record read
// through. `visit(child, leaf, row)` answers 1 to read a part's own parts, 0
// to go on past it and -1 to stop the walk, which then answers false.
template <class Visit>
bool each_part_below(AstBuildCtx& ctx, NodeId id, Visit&& visit) {
  const Cst& cst = *ctx.cst;
  SmallRun<NodeId, 16> pending;
  const auto open = [&](NodeId at) {
    for (const NodeId c : cst.reverse_children(at)) pending.push_back(c);
  };
  open(id);
  while (!pending.empty()) {
    ctx.stop.poll();
    const auto child = pending.back();
    pending.pop_back();
    const CstNode& n = cst[child];
    const bool leaf = n.subtreeSize == 1;
    if (n.hidden || (leaf && !part_leaf(ctx, n))) continue;
    const auto row = ctx.stamp.row(n);
    const int step = !leaf && n.recovered && !ctx.actions->is_call(row)
                         ? 1
                         : visit(child, leaf, row);
    if (step < 0) return false;
    if (step > 0) open(child);
  }
  return true;
}

// Text thunks only depend on the calls below their record. Discover that
// frontier without running converters, then evaluate it in source order.
inline void text_dependencies(AstBuildCtx& ctx, NodeId id,
                              std::vector<NodeId>& dependencies) {
  each_part_below(ctx, id, [&](NodeId child, bool leaf, ElementId row) {
    if (ctx.actions->call_at(row) == nullptr) return leaf ? 0 : 1;
    dependencies.push_back(child);
    return 0;
  });
}

// One query's conversions, in postorder on the heap. A converter that reads
// nothing has no dependencies; failures stop before later siblings run.
inline std::optional<std::string_view> evaluate_text(AstBuildCtx& ctx,
                                                     NodeId id, TextFn fn,
                                                     bool children) {
  if (ctx.textValues != nullptr) {
    const auto found = ctx.textValues->calls.find(id);
    if (found != ctx.textValues->calls.end() && found->second.fn == fn)
      return found->second.text;
  }
  if (!children) return fn(ctx, id);
  TextValues values;
  struct Scope {
    AstBuildCtx& ctx;
    TextValues* previous;
    ~Scope() { ctx.textValues = previous; }
  } scope{ctx, std::exchange(ctx.textValues, &values)};
  struct Frame {
    NodeId id;
    TextFn fn;
    std::size_t begin, next;
  };
  std::vector<Frame> frames;
  std::vector<NodeId> dependencies;
  const auto enter = [&](NodeId node, TextFn call, bool reads) {
    const auto begin = dependencies.size();
    if (reads) text_dependencies(ctx, node, dependencies);
    frames.push_back({node, call, begin, begin});
  };
  text_dependencies(ctx, id, dependencies);
  if (dependencies.empty()) return fn(ctx, id);
  frames.push_back({id, fn, 0, 0});
  for (;;) {
    Frame& current = frames.back();
    if (current.next != dependencies.size()) {
      const auto child = dependencies[current.next++];
      const auto row = ctx.stamp.row((*ctx.cst)[child]);
      const auto call = ctx.actions->call_at(row);
      if (const auto found = values.calls.find(child);
          found == values.calls.end() || found->second.fn != call)
        enter(child, call, ctx.actions->text_children(row, true));
      continue;
    }
    const Frame done = current;
    const auto value = done.fn(ctx, done.id);
    if (!value) return std::nullopt;
    frames.pop_back();
    dependencies.resize(done.begin);
    if (frames.empty()) return value;
    values.calls.try_emplace(done.id, done.fn, *value);
  }
}

// A call below a record as one part, converted where the record's text
// converts: 1 where it is not, -1 where its conversion failed.
template <bool Text>
int call_part(AstBuildCtx& ctx, NodeId child, ElementId row, PartRun& out) {
  const TextFn fn =
      converts_text<Text>(ctx) ? ctx.actions->call_at(row) : nullptr;
  if (fn == nullptr) return 1;
  const auto value =
      evaluate_text(ctx, child, fn, ctx.actions->text_children(row, true));
  if (!value) return -1;
  out.push_back(*value);
  return 0;
}

// The parts below `id`, not `id` itself: each data-rule call is one part,
// converted atomically, and a repaired wrapper record is transparent. Where
// `Flat` a raw subtree is read through, else it is flattened and joined once.
template <bool Text, bool Flat>
bool each_child_part(AstBuildCtx& ctx, NodeId id, PartRun& out) {
  return each_part_below(ctx, id, [&](NodeId child, bool leaf, ElementId row) {
    if (const int step = call_part<Text>(ctx, child, row, out); step <= 0)
      return step;
    if (leaf) {
      out.push_back(ctx.cst->text(child));
      return 0;
    }
    if constexpr (Flat) {
      return 1;
    } else {
      PartRun inner;
      if (!each_child_part<Text, true>(ctx, child, inner)) return -1;
      out.push_back(join_parts(ctx, inner));
      return 0;
    }
  });
}

// The payload's own text stops before the converter run at this site. `Flat`
// is for a text consumer; parts consumers retain the grouping above.
template <bool Text, bool Base, bool Flat = false>
bool each_part(AstBuildCtx& ctx, NodeId id, PartRun& out) {
  const Cst& cst = *ctx.cst;
  const CstNode& n = cst[id];
  const ElementId row = ctx.stamp.row(n);
  if constexpr (!Base)
    if (converts_text<Text>(ctx))
      if (const TextFn fn = ctx.actions->text_at(row)) {
        const auto part =
            evaluate_text(ctx, id, fn, ctx.actions->text_children(row, false));
        if (!part) return false;
        out.push_back(*part);
        return true;
      }
  if (n.subtreeSize != 1) return each_child_part<Text, Flat>(ctx, id, out);
  // A call that read nothing has no part; one that read a token, repaired or
  // not, has that token.
  if ((n.begin != n.end || !ctx.actions->is_call(row)) && part_leaf(ctx, n))
    out.push_back(cst.text(id));
  return true;
}

template <bool Text = true, bool Base = false>
CURSIV_COLD CURSIV_NOINLINE inline std::optional<PartRun> parts_of(
    AstBuildCtx& ctx, NodeId id) {
  PartRun run;
  if (!each_part<Text, Base>(ctx, id, run)) return std::nullopt;
  return run;
}

// A terminal has one raw part: its Parts converter must not call that same
// terminal's TextFn.
template <bool Data, bool Text = true, bool Base = false>
std::optional<PartRun> payload_parts(AstBuildCtx& ctx, NodeId id) {
  if constexpr (Data) {
    return parts_of<Text, Base>(ctx, id);
  } else {
    PartRun run;
    run.push_back(ctx.cst->text(id));
    return run;
  }
}

// What a parts converter reads: valid for the call.
inline Parts as_parts(const PartRun& run) { return {run.begin(), run.size()}; }
inline Parts as_parts(Parts parts) { return parts; }

// Skips and insertions contribute no text. Rescans keep the author's span,
// just as an assignment over that leaf does.
template <bool Text = true, bool Base = false>
CURSIV_NOINLINE inline std::optional<std::string_view> joined_parts(
    AstBuildCtx& ctx, NodeId id) {
  PartRun run;
  if (!each_part<Text, Base, true>(ctx, id, run)) return std::nullopt;
  return join_parts(ctx, run);
}

// The text one payload denotes. A data rule's raw spelling would carry the
// trivia between its parts, so the parts are joined instead, unless it is one
// visible leaf with no converter of its own.
template <bool Data, bool Text = true, bool Base = false>
std::optional<std::string_view> payload_text(AstBuildCtx& ctx, NodeId id) {
  if constexpr (Data) {
    const CstNode& n = (*ctx.cst)[id];
    if (n.subtreeSize == 1 && !n.hidden && !n.recovered) [[likely]] {
      if (Base || !converts_text<Text>(ctx) ||
          ctx.actions->text_at(ctx.stamp.row(n)) == nullptr)
        return ctx.cst->text(n);
    }
    return joined_parts<Text, Base>(ctx, id);
  } else {
    return ctx.cst->text(id);
  }
}

// Whether a repair invented the text, which then denotes nothing. One invented
// part of a data rule is enough; a dropped token is no part.
template <bool Data>
CURSIV_COLD bool never_written(const AstBuildCtx& ctx, NodeId id) {
  const Cst& cst = *ctx.cst;
  const auto invented = [&cst](NodeId i) {
    return cst[i].recovered && cst[i].begin == cst[i].end;
  };
  if (invented(id)) return true;
  if constexpr (Data)
    for (NodeId i = cst.subtree_begin(id); i < id; ++i) {
      ctx.stop.poll();
      if (invented(i) && !dropped_token(ctx, cst[i])) return true;
    }
  return false;
}

// How a converter's answer is taken: a positioned result that failed has said
// so in a diagnostic, any other answer is the value. `sink` takes the value.
template <class Res, class Sink>
CURSIV_ALWAYS_INLINE inline bool take_conversion(AstBuildCtx& ctx, NodeId id,
                                                 Res&& result, Sink&& sink) {
  if constexpr (is_conversion_result<std::remove_cvref_t<Res>>) {
    if (!result) {
      ctx.diags->push_back(ctx.place(std::forward<Res>(result).error, id));
      return false;
    }
    sink(*std::forward<Res>(result).value);
  } else {
    sink(std::forward<Res>(result));
  }
  return true;
}

// The converter `CR` run on the payload in the form it reads, its parts or its
// text, asked of `parts()` and `text()`; false where the payload yields none.
template <class CR, class Parts, class Text, class Sink>
CURSIV_ALWAYS_INLINE inline bool convert_with(AstBuildCtx& ctx, NodeId id,
                                              [[maybe_unused]] Parts parts,
                                              [[maybe_unused]] Text text,
                                              Sink&& sink) {
  const auto place_value = [&](auto&& result) {
    return take_conversion(ctx, id, std::forward<decltype(result)>(result),
                           sink);
  };
  if constexpr (ConvertsParts<CR>) {
    const auto p = parts();
    if (!p) return false;
    return place_value(CR::convert(as_parts(*p)));
  } else if constexpr (ConvertsText<CR>) {
    const auto t = text();
    if (!t) return false;
    return place_value(CR::convert(*t));
  } else {
    return place_value(CR::convert());
  }
}

// The type a value slot writes, told by address.
template <class T>
inline constexpr char value_tag = 0;

// The value a data rule passed on, from its record or the one record under it
// that holds a value: the record of the branch it took.
template <class T>
bool pass_value(AstBuildCtx& ctx, NodeId id, std::optional<T>& out) {
  const Cst& cst = *ctx.cst;
  for (NodeId at = id;;) {
    const ValueSlot* const slot = ctx.actions->value_at(ctx.stamp.row(cst[at]));
    if (slot != nullptr && slot->fn != nullptr)
      return slot->type == &value_tag<T> && slot->fn(ctx, at, &out);
    NodeId only = kNoNode;
    for (const NodeId child : cst.reverse_children(at)) {
      const CstNode& n = cst[child];
      if (n.hidden || dropped_token(ctx, n)) continue;
      if (only != kNoNode) return false;
      only = child;
    }
    if (only == kNoNode) return false;
    at = only;
  }
}

template <class CR, class M, std::size_t Alt, bool Data, bool Text = true,
          bool Guarded = true, bool Base = false>
bool invoke_conv(AstBuildCtx& ctx, NodeId id, void* out);

// What a record holds for a rule that passes its value on: its own rule's
// conversion, into that rule's type.
template <class R>
consteval ValueSlot value_slot() {
  using T = rule_type_t<R>;
  return {
      &invoke_conv<converter_rule_t<R>, std::optional<T>, 0, is_data_rule<R>>,
      &value_tag<T>};
}

// What a payload converts to, on every path.
template <class CR, class M, std::size_t Alt, bool Data, bool Text,
          bool Guarded, bool Base>
CURSIV_ALWAYS_INLINE inline bool convert_payload(AstBuildCtx& ctx, NodeId id,
                                                 void* out) {
  using A = typename conv_alt_at<typename strip_storage<M>::type, Alt>::type;
  M& slot = *static_cast<M*>(out);

  // The member keeps its default, and no second sentence is added.
  if constexpr (Guarded)
    if (ctx.repaired) [[unlikely]]
      if (never_written<Data>(ctx, id)) return false;

  // Always inline: the thunk is reached through a pointer, and a body left
  // out of line is a second call per converted value.
  const auto store = [&](auto&& value) CURSIV_ALWAYS_INLINE {
    // A view of a string the converter built: the tree keeps the bytes.
    if constexpr (std::is_same_v<A, std::string_view> &&
                  std::is_same_v<std::remove_cvref_t<decltype(value)>,
                                 std::string>)
      place<A>(slot, ctx.keep_text(value));
    else
      place<A>(slot, std::forward<decltype(value)>(value));
  };

  if constexpr (is_pass_conv<CR>) {
    std::optional<typename CR::type> value;
    if (!pass_value(ctx, id, value)) return false;
    store(*std::move(value));
    return true;
  } else if constexpr (!std::is_void_v<CR>) {
    return convert_with<CR>(
        ctx, id, [&] { return payload_parts<Data, Text, Base>(ctx, id); },
        [&] { return payload_text<Data, Text, Base>(ctx, id); }, store);
  } else {
    // `admits` refuses a payload with neither a converter nor a default
    // conversion, so this branch is the latter, or a class made from the string
    // a data rule passes on.
    static_assert(
        has_default_convert<A> || std::is_constructible_v<A, std::string>,
        "a value member is vocabulary or written by a converter");
    // A text is its own value and the exact decimal path answers first: only
    // what they decline pays for the result's diagnostic.
    const auto place_value = [&](std::string_view text) CURSIV_ALWAYS_INLINE {
      if constexpr (std::is_same_v<A, std::string_view> ||
                    std::is_same_v<A, std::string>) {
        place<A>(slot, text);
        return true;
      } else if constexpr (!has_default_convert<A>) {
        place<A>(slot, std::string(text));
        return true;
      } else {
        if constexpr (std::is_same_v<A, double>) {
          double fast = 0.0;
          if (fast_decimal(text, fast)) {
            place<A>(slot, fast);
            return true;
          }
        }
        return take_conversion(ctx, id, default_convert<A>(text), store);
      }
    };
    // One token's text is never absent, and a view passed on is held in
    // registers where an optional one goes through memory: the member copy
    // that follows would then wait on its two halves.
    if constexpr (Data) {
      const auto text = payload_text<Data, Text, Base>(ctx, id);
      return text && place_value(*text);
    } else {
      return place_value(ctx.cst->text(id));
    }
  }
}
template <class CR, class M, std::size_t Alt, bool Data, bool Text,
          bool Guarded, bool Base>
CURSIV_NOINLINE bool convert_payload_slow(AstBuildCtx& ctx, NodeId id,
                                          void* out) {
  return convert_payload<CR, M, Alt, Data, Text, Guarded, Base>(ctx, id, out);
}

// One symbol per (converter, member) pair; `out` points at `conv_target_t`. A
// data rule read as one clean leaf converts as a terminal does, inline.
template <class CR, class M, std::size_t Alt, bool Data, bool Text,
          bool Guarded, bool Base>
bool invoke_conv(AstBuildCtx& ctx, NodeId id, void* out) {
  if constexpr (Data && (std::is_void_v<CR> ||
                         (converts_input<CR> && !ConvertsParts<CR>))) {
    if (!ctx.repaired) [[likely]] {
      const CstNode& n = (*ctx.cst)[id];
      if (n.subtreeSize == 1 && !n.hidden && !n.recovered &&
          (Base || !Text ||
           ctx.actions->text_at(ctx.stamp.row(n)) == nullptr)) [[likely]]
        return convert_payload<CR, M, Alt, false, Text, false, Base>(ctx, id,
                                                                     out);
    }
    return convert_payload_slow<CR, M, Alt, Data, Text, Guarded, Base>(ctx, id,
                                                                       out);
  } else {
    return convert_payload<CR, M, Alt, Data, Text, Guarded, Base>(ctx, id, out);
  }
}

// What a leaf denotes, its own converter applied: a data rule joins what its
// parts mean, not the bytes they occupy. The leaves it is asked of are read
// text, never one a repair invented.
template <class Cv, bool Data = false>
std::optional<std::string_view> denoted_text(AstBuildCtx& ctx, NodeId id) {
  std::string_view value;
  if (invoke_conv<Cv, std::string_view, 0, Data, true, false, true>(ctx, id,
                                                                    &value))
    return value;
  return std::nullopt;
}

// One converter run over a text a conversion before it made.
template <class Cv>
std::optional<std::string_view> convert_text(AstBuildCtx& ctx, NodeId id,
                                             std::string_view text) {
  std::optional<std::string_view> out;
  convert_with<Cv>(
      ctx, id, [&] { return std::optional<Parts>(Parts{&text, 1}); },
      [&] { return std::optional(text); },
      [&](auto&& value) {
        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>,
                                     std::string>)
          out = ctx.keep_text(value);
        else
          out = std::string_view(value);
      });
  return out;
}

// One converter of a record's chain: the rule's own, a data rule's or not.
// The lead is the record's first rule, which reads the record; the others read
// the text before them.
template <class Cv, bool Data, bool Lead>
struct text_step {
  using conv = Cv;
  static constexpr bool data = Data;
  static constexpr bool lead = Lead;
};

// A record's text through the converters of the rules it stands for, in
// order: a token's, then those of the unit rules over it. `BaseData` says how
// the record reads when the lead converts nothing.
template <bool BaseData, class First, class... Rest>
std::optional<std::string_view> denoted_chain(AstBuildCtx& ctx, NodeId id) {
  std::optional<std::string_view> value;
  if constexpr (First::lead) {
    value = denoted_text<typename First::conv, First::data>(ctx, id);
  } else {
    // A converter that reads nothing is not handed the record's text, so the
    // calls below the record are not run for it.
    std::string_view base;
    if constexpr (converts_input<typename First::conv>) {
      const auto text = payload_text<BaseData, true, true>(ctx, id);
      if (!text) return std::nullopt;
      base = *text;
    }
    value = convert_text<typename First::conv>(ctx, id, base);
  }
  ((value = value ? convert_text<typename Rest::conv>(ctx, id, *value) : value),
   ...);
  return value;
}

// Whether a converter answers text, either form: reading the raw bytes for it
// would make a terminal mean one thing assigned and another joined.
template <class Cv>
concept DenotesText = [] {
  if constexpr (ConvertsParts<Cv>)
    return is_text_value<
        unwrap_result_t<typename convert_parts_result<Cv>::type>>;
  else if constexpr (ConvertsText<Cv> || ConvertsNothing<Cv>)
    return is_text_value<unwrap_result_t<typename convert_result<Cv>::type>>;
  else
    return false;
}();

// A value the grammar states with `as<V>`, stored plainly in `H`, what the
// member holds. Any other converter is called, even one that reads nothing.
template <class CR, class H>
concept StatesValue =
    is_value_conv<CR> && !is_variant_v<H> && std::is_trivially_copyable_v<H> &&
    std::is_same_v<std::remove_cvref_t<decltype(CR::convert())>, H>;

// One slot of the conversion registry: the thunk, and the value where the
// grammar stated one.
template <class CR, class M, std::size_t Alt, bool Data, bool Text = true>
consteval ConvSlot conv_slot() {
  using H = typename strip_storage<M>::type;
  // No conversion, joining or ownership change: the writer can take the
  // record text itself without an erased call and its output object.
  if constexpr (std::is_void_v<CR> && Alt == 0 && !Data &&
                std::is_same_v<H, std::string_view>) {
    return {};
  } else {
    ConvSlot s{&invoke_conv<CR, M, Alt, Data, Text>, nullptr};
    if constexpr (Alt == 0 && !Data && StatesValue<CR, H>)
      s.value = &kConvValue<CR, H>;
    return s;
  }
}

// A write that passes on what its record holds, where the holding rule `R` is
// known: R's conversion straight into the member, as `pass_value` would find.
template <class R, class Pass, class M, std::size_t Alt, bool Data, bool Text>
consteval ConvSlot held_slot() {
  if constexpr (std::is_same_v<rule_type_t<R>, typename Pass::type>)
    return conv_slot<converter_rule_t<R>, M, Alt, is_data_rule<R>>();
  else
    return conv_slot<Pass, M, Alt, Data, Text>();
}

}  // namespace cursiv::engine
