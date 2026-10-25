// The symbols an `AstActions` names: a node factory and a conversion, each
// templated on the strict minimum, so the code behind the table's function
// pointers is shared by every grammar in the program.
#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include "cursiv/core/grammar/Conversion.hpp"
#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstArena.hpp"
#include "cursiv/core/syntax-tree/AstField.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/utils/macros.hpp"
#include "cursiv/engines/shared/runtime/AstActions.hpp"
#include "cursiv/engines/shared/runtime/AstBuildCtx.hpp"
#include "cursiv/engines/shared/runtime/SmallRun.hpp"
#include "cursiv/engines/shared/runtime/ast_conv.hpp"
#include "cursiv/engines/shared/table/StampLayout.hpp"

namespace cursiv::detail {

// One node factory per AST type. The arena stamps the tag and registers the
// destructor, so a node built by hand carries identity too.
template <class T>
AstNode* make_node(AstBuildCtx& ctx, NodeId id) {
  AstNode* const base = ctx.arena->template make<T>();
  base->cstNodeId = id;
  ctx.nodes->push_back(base);
  return base;
}

using PartRun = SmallRun<std::string_view, 8>;

// Whether the conversions of a record's text may run: the grammar converts
// text below it, or a repair left leaves no static closure speaks for.
template <bool Text>
bool texting(const AstBuildCtx& ctx) {
  return Text || ctx.repaired;
}

// A leaf that gives its parent a part: read text, or a word a repair read as
// the one it misspells -- a skip carries no state and an insertion no width.
// A token laid with no width gives none; a call that read nothing gives one.
inline bool part_leaf(const AstBuildCtx& ctx, const CstNode& n) {
  if (n.hidden) return false;
  if (n.recovered) return n.begin != n.end && ctx.stamp.state(n) != 0;
  return n.begin != n.end || ctx.actions->is_call(ctx.stamp.row(n));
}

// The parts in one text. Adjacent in the source, they are a view of it.
inline std::string_view join_parts(AstBuildCtx& ctx, const PartRun& run) {
  std::size_t total = 0;
  const char* first = nullptr;
  const char* end = nullptr;
  bool verbatim = true;
  for (const std::string_view part : run) {
    if (part.empty()) continue;
    total += part.size();
    if (first == nullptr)
      first = part.data();
    else if (part.data() != end)
      verbatim = false;
    end = part.data() + part.size();
  }
  if (total == 0) return std::string_view{};
  if (verbatim) return std::string_view(first, total);
  char* const buf = static_cast<char*>(ctx.mem->allocate(total, alignof(char)));
  char* out = buf;
  for (const std::string_view part : run)
    out = std::copy(part.begin(), part.end(), out);
  return std::string_view(buf, total);
}

template <bool Text>
bool each_child_part(AstBuildCtx& ctx, NodeId id, PartRun& out);

// What a record gives its parent as a part. A call is one: its text through
// the conversions of the rules it stands for when one converts text, else its
// own parts joined; a repair's span is not a call, its leaves count as they
// stand.
template <bool Text>
CURSIV_NOINLINE bool child_part(AstBuildCtx& ctx, NodeId id, PartRun& out) {
  const Cst& cst = *ctx.cst;
  const CstNode& n = cst[id];
  if (n.hidden) return true;
  const ElementId row = ctx.stamp.row(n);
  const bool leaf = n.subtreeSize == 1;
  if (leaf) {
    if (!part_leaf(ctx, n)) return true;
  } else if (n.recovered && !ctx.actions->is_call(row)) {
    return each_child_part<Text>(ctx, id, out);
  }
  if (texting<Text>(ctx))
    if (const TextFn fn = ctx.actions->call_at(row)) {
      const auto part = fn(ctx, id);
      if (!part) return false;
      out.push_back(*part);
      return true;
    }
  if (leaf) {
    out.push_back(cst.text(id));
    return true;
  }
  PartRun inner;
  if (!each_child_part<Text>(ctx, id, inner)) return false;
  out.push_back(join_parts(ctx, inner));
  return true;
}

template <bool Text>
bool each_child_part(AstBuildCtx& ctx, NodeId id, PartRun& out) {
  const Cst& cst = *ctx.cst;
  SmallRun<NodeId, 16> kids;
  for (const NodeId child : cst.reverse_children(id)) kids.push_back(child);
  for (std::size_t k = kids.size(); k-- > 0;)
    if (!child_part<Text>(ctx, kids[k], out)) return false;
  return true;
}

// The parts of a payload. A record that stands for rules whose converters
// yield text is one part: that text. `Base` leaves the record's own
// conversions out, for the conversion that is computing them.
template <bool Text, bool Base>
bool each_part(AstBuildCtx& ctx, NodeId id, PartRun& out) {
  const Cst& cst = *ctx.cst;
  const CstNode& n = cst[id];
  const ElementId row = ctx.stamp.row(n);
  if constexpr (!Base)
    if (texting<Text>(ctx))
      if (const TextFn fn = ctx.actions->text_at(row)) {
        const auto part = fn(ctx, id);
        if (!part) return false;
        out.push_back(*part);
        return true;
      }
  if (n.subtreeSize != 1) return each_child_part<Text>(ctx, id, out);
  if (!ctx.actions->is_call(row) && part_leaf(ctx, n))
    out.push_back(cst.text(id));
  return true;
}

// Materialised in the tree's arena and not on a frame: what a `parts_conv`
// receives outlives the call that builds it.
template <bool Text = true, bool Base = false>
CURSIV_COLD
    CURSIV_NOINLINE inline std::optional<std::span<const std::string_view>>
    parts_of(AstBuildCtx& ctx, NodeId id) {
  PartRun run;
  if (!each_part<Text, Base>(ctx, id, run)) return std::nullopt;
  if (run.size() == 0) return std::span<const std::string_view>{};
  auto* const out = static_cast<std::string_view*>(ctx.mem->allocate(
      run.size() * sizeof(std::string_view), alignof(std::string_view)));
  for (std::size_t i = 0; i < run.size(); ++i) out[i] = run[i];
  return std::span<const std::string_view>{out, run.size()};
}

// A terminal's one raw part: its Parts converter must not call that same
// terminal's TextFn.
inline std::span<const std::string_view> terminal_parts(AstBuildCtx& ctx,
                                                        NodeId id) {
  auto* const part = static_cast<std::string_view*>(
      ctx.mem->allocate(sizeof(std::string_view), alignof(std::string_view)));
  *part = ctx.cst->text(id);
  return {part, 1};
}

template <bool Data, bool Text = true, bool Base = false>
std::optional<std::span<const std::string_view>> payload_parts(AstBuildCtx& ctx,
                                                               NodeId id) {
  if constexpr (Data)
    return parts_of<Text, Base>(ctx, id);
  else
    return terminal_parts(ctx, id);
}

// Skips and insertions contribute no text. Rescans keep the author's span,
// just as an assignment over that leaf does.
template <bool Text = true, bool Base = false>
CURSIV_NOINLINE inline std::optional<std::string_view> joined_parts(
    AstBuildCtx& ctx, NodeId id) {
  PartRun run;
  if (!each_part<Text, Base>(ctx, id, run)) return std::nullopt;
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
      if (Base || !texting<Text>(ctx) ||
          ctx.actions->text_at(ctx.stamp.row(n)) == nullptr)
        return ctx.cst->text(id);
    }
    return joined_parts<Text, Base>(ctx, id);
  } else {
    return ctx.cst->text(id);
  }
}

// Text that was never written denotes nothing: a repair may invent the
// zero-width record a missing terminal would have had, and the `inserted`
// diagnostic already said so. One invented part of a data rule is enough.
template <bool Data>
CURSIV_COLD bool never_written(const AstBuildCtx& ctx, NodeId id) {
  const Cst& cst = *ctx.cst;
  const auto invented = [&cst](NodeId i) {
    return cst[i].recovered && cst[i].begin == cst[i].end;
  };
  if (invented(id)) return true;
  if constexpr (Data)
    for (NodeId i = cst.subtree_begin(id); i < id; ++i)
      if (invented(i)) return true;
  return false;
}

// Whether a conversion failed, its diagnostic then said.
template <class Res>
CURSIV_ALWAYS_INLINE inline bool conversion_failed(AstBuildCtx& ctx, NodeId id,
                                                   Res&& result) {
  if (result) return false;
  ctx.diags->push_back(ctx.place(std::forward<Res>(result).error, id));
  return true;
}

// What a converter answered, settled: a positioned result that failed has said
// so in a diagnostic, any other answer is the value. `sink` takes the value.
template <class Res, class Sink>
CURSIV_ALWAYS_INLINE inline bool conversion_settled(AstBuildCtx& ctx, NodeId id,
                                                    Res&& result, Sink&& sink) {
  if constexpr (is_conversion_result<std::remove_cvref_t<Res>>) {
    if (conversion_failed(ctx, id, std::forward<Res>(result))) return false;
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
  const auto settle = [&](auto&& result) {
    return conversion_settled(ctx, id, std::forward<decltype(result)>(result),
                              sink);
  };
  if constexpr (ConvertsPartsMem<CR> || ConvertsParts<CR>) {
    const auto p = parts();
    if (!p) return false;
    if constexpr (ConvertsPartsMem<CR>)
      return settle(CR::convert(*p, ctx.mem));
    else
      return settle(CR::convert(*p));
  } else if constexpr (ConvertsTextMem<CR> || ConvertsText<CR>) {
    const auto t = text();
    if (!t) return false;
    if constexpr (ConvertsTextMem<CR>)
      return settle(CR::convert(*t, ctx.mem));
    else
      return settle(CR::convert(*t));
  } else {
    return settle(CR::convert());
  }
}

// One symbol per (converter, member) pair. `out` points at the member's type,
// `conv_target_t` on both sides.
template <class CR, class M, std::size_t Alt, bool Data, bool Text = true,
          bool Guarded = true, bool Base = false>
bool invoke_conv(AstBuildCtx& ctx, NodeId id, void* out) {
  using A = typename conv_alt_at<M, Alt>::type;
  M& slot = *static_cast<M*>(out);

  // The member keeps its default, and no second sentence is added.
  if constexpr (Guarded)
    if (ctx.repaired) [[unlikely]]
      if (never_written<Data>(ctx, id)) return false;

  // Always inline: the thunk is reached through a pointer, and a body left
  // out of line is a second call per converted value.
  const auto store = [&](auto&& value) CURSIV_ALWAYS_INLINE {
    conv_store<M, A>(slot, std::forward<decltype(value)>(value));
  };

  if constexpr (!std::is_void_v<CR>) {
    // A converter that materialises allocates in the tree's resource, so its
    // view lives as long as the ParseResult.
    return convert_with<CR>(
        ctx, id, [&] { return payload_parts<Data, Text, Base>(ctx, id); },
        [&] { return payload_text<Data, Text, Base>(ctx, id); }, store);
  } else {
    // `admits` refuses a payload with neither a converter nor a default
    // conversion, so this branch is the latter.
    static_assert(has_default_convert<A>,
                  "a value member is vocabulary or written by a converter");
    const auto text = payload_text<Data, Text, Base>(ctx, id);
    if (!text) return false;
    // A text is its own value and the exact decimal path answers first: only
    // what they decline pays for the result's diagnostic.
    if constexpr (std::is_same_v<A, std::string_view>) {
      conv_store<M, A>(slot, *text);
      return true;
    }
    if constexpr (std::is_same_v<A, double>) {
      double fast = 0.0;
      if (detail::fast_decimal(*text, fast)) {
        conv_store<M, A>(slot, fast);
        return true;
      }
    }
    return conversion_settled(ctx, id, default_convert<A>(*text), store);
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
      [&](std::string_view value) { out = value; });
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
    const auto base = payload_text<BaseData, true, true>(ctx, id);
    if (!base) return std::nullopt;
    value = convert_text<typename First::conv>(ctx, id, *base);
  }
  ((value = value ? convert_text<typename Rest::conv>(ctx, id, *value) : value),
   ...);
  return value;
}

// Whether a converter answers a view of its own, either form: reading the raw
// bytes for it would make a terminal mean one thing assigned and another
// joined.
template <class Cv>
concept DenotesText = [] {
  if constexpr (ConvertsPartsMem<Cv> || ConvertsParts<Cv>)
    return std::is_same_v<
        unwrap_result_t<typename convert_parts_result<Cv>::type>,
        std::string_view>;
  else if constexpr (ConvertsTextMem<Cv> || ConvertsText<Cv> ||
                     ConvertsNothing<Cv>)
    return std::is_same_v<unwrap_result_t<typename convert_result<Cv>::type>,
                          std::string_view>;
  else
    return false;
}();

// A converter that reads nothing answers one constant value. Only a member
// it can be stored into plainly qualifies: a variant and a reference stay
// with the call.
template <class CR, class M>
concept StatesValue =
    ConvertsNothing<CR> && !ConvertsParts<CR> && !ConvertsPartsMem<CR> &&
    !ConvertsText<CR> && !ConvertsTextMem<CR> && !is_variant_v<M> &&
    std::is_trivially_copyable_v<M> &&
    std::is_same_v<std::remove_cvref_t<decltype(CR::convert())>, M> &&
    requires { std::bool_constant<(CR::convert(), true)>{}; };

// One slot of the conversion registry: the thunk, and the value where the
// grammar stated one.
template <class CR, class M, std::size_t Alt, bool Data, bool Text = true>
consteval ConvSlot conv_slot() {
  ConvSlot s{&invoke_conv<CR, M, Alt, Data, Text>, nullptr};
  if constexpr (Alt == 0 && !Data && StatesValue<CR, M>)
    s.value = &kConvValue<CR, M>;
  return s;
}

}  // namespace cursiv::detail
