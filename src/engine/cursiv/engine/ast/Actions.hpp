// A grammar's actions as code: what a record does to the tree, composed from
// the rows its element names, and the write of each row.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "cursiv/engine/ast/AstBuilder.hpp"
#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/grammar/PayloadOf.hpp"
#include "cursiv/engine/grammar/Row.hpp"
#include "cursiv/grammar/Conversion.hpp"
#include "cursiv/syntax-tree/AstField.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/VariantNodeAlt.hpp"
#include "cursiv/utils/CaseFold.hpp"
#include "cursiv/utils/TypeName.hpp"
#include "cursiv/utils/macros.hpp"
#include "cursiv/utils/typelist.hpp"

namespace cursiv::engine {

// What a conversion answered, handed to `sink`: a positioned result that
// failed says so at record `at`.
template <class Result, class Sink>
CURSIV_ALWAYS_INLINE inline bool take_result(AstBuilder& b, NodeId at, Result&& result,
                                             Sink&& sink) {
  if constexpr (requires { result.value.has_value(); result.error.message; }) {
    if (result) {
      sink(std::move(*result.value));
    } else {
      b.fail(std::move(result.error.message), at);
      return false;
    }
  } else {
    sink(std::forward<Result>(result));
  }
  return true;
}

// A type as the address that stands for it.
template <class T>
inline constexpr char value_tag = 0;

// The converter `Cv` run on what a record reads, `in`: its parts where it
// takes them, else their text. False where it answered none.
template <class Cv, std::size_t N, class Sink>
bool convert_parts(AstBuilder& b, NodeId id, std::span<const std::string_view, N> in, Sink&& sink) {
  if constexpr (N == 1 && detail::ConvertsParts<Cv>)
    return take_result(b, id, Cv::convert(std::span<const std::string_view>{in}), sink);
  else if constexpr (detail::ConvertsParts<Cv>)
    return take_result(b, id, Cv::convert(in), sink);
  else if constexpr (N == 1 && detail::ConvertsText<Cv>)
    return take_result(b, id, Cv::convert(in.front()), sink);
  else if constexpr (detail::ConvertsText<Cv>)
    return take_result(b, id, Cv::convert(b.join(in)), sink);
  else
    return take_result(b, id, Cv::convert(), sink);
}

// The templates below take a language's `Does`, which names its rows as
// `Does::rows`, never the rows themselves: a compiler hashes a template's
// arguments, and the rows hold the whole grammar.
// The rule the conversion `I` of the rows is of, and its converter.
template <class Does, std::uint16_t I>
using rule_at = typename detail::tl_at_t<I, typename Does::rows>::source;
template <class Does, std::uint16_t I>
using converter_at = detail::converter_rule_t<rule_at<Does, I>>;

// One step of a record's text, `in` read and become the text given: the
// conversion `I` of the rows where its rule converts text; else the call of
// that rule, one part of what reads it, its parts joined.
template <class Does, std::uint16_t I>
CURSIV_ALWAYS_INLINE inline bool text_step(AstBuilder& b, NodeId id,
                                           std::span<const std::string_view>& in,
                                           std::string_view& text) {
  if constexpr (converts<rule_at<Does, I>>() == Converts::text) {
    const auto keep = [&](auto&& value) {
      if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, std::string_view>)
        text = value;
      else
        text = b.keep(value);
    };
    if (!convert_parts<converter_at<Does, I>>(b, id, in, keep)) return false;
  } else {
    text = b.join(in);
  }
  in = {&text, 1};
  return true;
}

// Whether the step `I` of the rows reads what it is handed: a converter of
// text that takes no input reads nothing.
template <class Does, std::uint16_t I>
consteval bool step_reads() {
  using Cv = converter_at<Does, I>;
  return converts<rule_at<Does, I>>() != Converts::text || detail::ConvertsParts<Cv> ||
         detail::ConvertsText<Cv>;
}

// The text of a record through the steps `Is` of the rows, the innermost
// first: the first reads the record's parts, each next one the text before
// it. The steps before one that reads nothing are not run.
template <class Does, std::uint16_t... Is>
bool text_chain(AstBuilder& b, [[maybe_unused]] NodeId id,
                std::span<const std::string_view> parts, std::string_view& out) {
  // A record with no step of its own gives its parts joined: those were
  // converted on the way.
  if constexpr (sizeof...(Is) == 0) out = b.join(parts);
  constexpr std::size_t start = [] {
    constexpr bool reads[] = {step_reads<Does, Is>()..., true};
    std::size_t last = 0;
    for (std::size_t at = 0; at < sizeof...(Is); ++at)
      if (!reads[at]) last = at;
    return last;
  }();
  return [&]<std::size_t... Js>(std::index_sequence<Js...>) {
    return ((Js < start || text_step<Does, Is>(b, id, parts, out)) && ...);
  }(std::make_index_sequence<sizeof...(Is)>{});
}
template <class Does, std::uint16_t... Is>
consteval AstText text_slot() {
  return {&text_chain<Does, Is...>, (step_reads<Does, Is>() && ...), sizeof...(Is) == 0};
}

// The value the conversion `I` of the rows gives a record, read from its
// parts through the text conversions `Ts` before it; `out` is an `optional`
// of it.
template <class Does, std::uint16_t I, std::uint16_t... Ts>
bool value_read(AstBuilder& b, NodeId id, void* out) {
  using Cv = converter_at<Does, I>;
  using V = detail::rule_type_t<rule_at<Does, I>>;
  std::span<const std::string_view> in;
  [[maybe_unused]] std::string_view text;
  if constexpr (detail::ConvertsParts<Cv> || detail::ConvertsText<Cv>) {
    if constexpr ((step_reads<Does, Ts>() && ...))
      if (!b.parts(id, in)) return false;
    if constexpr (sizeof...(Ts) != 0) {
      if (!text_chain<Does, Ts...>(b, id, in, text)) return false;
      in = {&text, 1};
    }
  }
  return convert_parts<Cv>(b, id, in, [&](auto&& value) {
    static_cast<std::optional<V>*>(out)->emplace(std::forward<decltype(value)>(value));
  });
}
template <class Does, std::uint16_t I, std::uint16_t... Ts>
consteval AstValue value_slot() {
  return {&value_tag<detail::rule_type_t<rule_at<Does, I>>>, &value_read<Does, I, Ts...>};
}

// What `text`, record `at`'s, gives a member of type `H` with no converter of
// its own: its default conversion, else the string a class is built from. An
// enum also takes the enumerator named in any letter case where each token of
// the record was read so.
template <class H>
CURSIV_ALWAYS_INLINE inline auto default_value(AstBuilder& b, NodeId at, std::string_view text) {
  if constexpr (!requires { default_converter<H>::convert(std::string_view{}); }) {
    static_assert(std::is_constructible_v<H, std::string>,
                  "a value member has a default conversion or is built from a string");
    return std::string(text);
  } else {
    auto result = default_converter<H>::convert(text);
    if constexpr (std::is_enum_v<H> && !detail::written_converter<H>()) {
      if (H named{}; !result && b.folds(at) &&
                     detail::find_enumerator<H>(text, named, detail::enum_probes{}, &foldedEqual))
        return decltype(result)::ok(named);
    }
    return result;
  }
}

// The value record `at` denotes for a write that reads `Src` into `H`, none
// for text a repair invented. A token's text goes through its terminal's
// converter here. What a data rule read goes through the conversions of its
// record, which the tables hold: those of the rules read inside it, then its
// own.
template <class Src, class H, class Sink>
CURSIV_ALWAYS_INLINE inline void read_value(AstBuilder& b, NodeId at, Sink&& sink) {
  using R = typename Src::rule;
  using Cv = detail::converter_rule_t<R>;
  constexpr bool data = [] {
    if constexpr (std::is_void_v<R>)
      return false;
    else
      return detail::is_data_rule<R>;
  }();
  if constexpr (!data) {
    const std::string_view text = b.cst.text(at);
    // A token holds some text: one with none is a repair's.
    if (text.empty()) [[unlikely]] return;
    if constexpr (std::is_void_v<Cv>) {
      if constexpr (std::is_same_v<H, std::string_view>)
        sink(text);
      else
        take_result(b, at, default_value<H>(b, at, text), sink);
    } else {
      convert_parts<Cv>(b, at, std::span<const std::string_view, 1>{&text, 1}, sink);
    }
  } else if constexpr (converts<R>() == Converts::value || converts<R>() == Converts::pass) {
    using V = detail::rule_type_t<R>;
    std::optional<V> value;
    if (b.value(at, &value_tag<V>, &value)) sink(std::move(*value));
  } else {
    std::string_view text;
    if (!b.denoted(at, text)) return;
    if constexpr (std::is_void_v<Cv> && !std::is_same_v<H, std::string_view>)
      take_result(b, at, default_value<H>(b, at, text), sink);
    else
      sink(text);
  }
}

// What a write of `Src` reads for a member `M` that holds `X`: `X`, or for a
// variant the alternative that takes the value. That is the member or the
// variant whole, or the alternative of the value's own type, else the first
// the value converts to.
template <class M, class X, class Src>
struct written_as {
  using type = X;
};
template <class M, class... As, class Src>
struct written_as<M, std::variant<As...>, Src> {
  using V = std::remove_cvref_t<typename detail::text_value<typename Src::rule>::type>;
  template <class A>
  static constexpr bool takes = [] {
    if constexpr (std::is_pointer_v<A>)
      return false;
    else if constexpr (detail::text_value<typename Src::rule>::converted)
      return std::is_constructible_v<A, V>;
    else
      return std::is_same_v<A, std::string_view> ||
             requires { default_converter<A>::convert(std::string_view{}); };
  }();
  static constexpr bool exact = (std::is_same_v<As, V> || ...);
  template <class... Bs>
  struct first {
    using type = void;
  };
  template <class B, class... Bs>
  struct first<B, Bs...> {
    using type = typename detail::cond_t<exact ? std::is_same_v<B, V> : takes<B>,
                                         std::type_identity<B>, first<Bs...>>::type;
  };
  using type = detail::cond_t<std::is_same_v<V, M> || std::is_same_v<V, std::variant<As...>>, V,
                              typename first<As...>::type>;
};

// The write of a row that names a member.
template <class R>
struct RowWrite {
  static constexpr AstWrite run = nullptr;
};
template <WriteKind K, class T, auto P, class N, class Src>
struct RowWrite<Row<K, T, detail::field_tag<P>, N, Src>> {
  using F = detail::field_tag<P>;
  using X = typename F::held;
  static constexpr bool valueIsNode = Row<K, T, detail::field_tag<P>, N, Src>::node;
  // The classes of node the member holds, as its alternatives hold them.
  using Nodes = detail::cond_t<F::content == detail::FieldContent::variant,
                       typename detail::variant_node_alt<X>::type, TypeList<X>>;

  // Puts `node` into the member as the first alternative whose class it is.
  template <class M, class... As>
  static bool place_node(M& member, AstNode* node, TypeList<As...>) {
    return ((node->type->derivesFrom(&kAstTypeTag<std::remove_pointer_t<As>>)
                 ? (detail::place<As>(member,
                                      detail::ast_cast_unchecked<std::remove_pointer_t<As>>(node)),
                    true)
                 : false) ||
            ...);
  }

  static bool takes(const AstTypeTag* type) noexcept {
    if constexpr (valueIsNode)
      return []<class... As>(const AstTypeTag* t, TypeList<As...>) {
        return (t->derivesFrom(&kAstTypeTag<std::remove_pointer_t<As>>) || ...);
      }(type, Nodes{});
    else
      return false;
  }

  // The write into a node whose class is unknown: false where it is not the
  // member's.
  static bool run(AstBuilder& b, AstNode* owner, const Pending& item) {
    if (!owner->type->derivesFrom(&kAstTypeTag<typename F::owner>)) return false;
    return into(b, detail::ast_cast_unchecked<typename F::owner>(owner), item);
  }

  // The write of a waiting value into `node`, of the member's class or one
  // derived from it: a list allocates once, for what waits to go into it.
  template <class Node>
  CURSIV_ALWAYS_INLINE static bool into(AstBuilder& b, Node* node, const Pending& item) {
    if constexpr (F::storage == detail::FieldStorage::vector)
      if (auto& member = node->*P; member.capacity() == 0) member.reserve(b.waiting(item));
    return put(b, node, item);
  }

  // The value `item` names, into the member of `node`.
  template <class Node>
  CURSIV_ALWAYS_INLINE static bool put(AstBuilder& b, Node* node, const Pending& item) {
    AstNode* const owner = node;
    auto& member = node->*P;
    if constexpr (K == WriteKind::flag) {
      if (!b.repaired || !b.invented(item.record)) [[likely]]
        detail::place<bool>(member, true);
    } else if constexpr (valueIsNode) {
      if (item.node == nullptr) return false;
      // A member of one class takes the node as the grammar's types say it
      // is; a variant asks the node which alternative it is.
      if constexpr (F::content == detail::FieldContent::variant) {
        if (!place_node(member, item.node, Nodes{})) return false;
      } else {
        detail::place<X>(member,
                         detail::ast_cast_unchecked<std::remove_pointer_t<X>>(item.node));
      }
      b.out.attach(*item.node, *owner);
    } else if constexpr (detail::is_ref_content(F::content)) {
      const NodeId at = b.payload(item.record);
      // A name a converter built is text the tree keeps.
      read_value<Src, std::string_view>(b, at, [&](auto&& text) {
        std::string_view name;
        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(text)>, std::string_view>)
          name = text;
        else
          name = b.keep(text);
        X& slot = [&]() -> X& {
          if constexpr (F::storage == detail::FieldStorage::direct)
            return member;
          else if constexpr (F::storage == detail::FieldStorage::optional)
            return member.emplace();
          else
            return member.emplace_back();
        }();
        std::uint32_t index = 0;
        if constexpr (F::storage == detail::FieldStorage::vector)
          index = static_cast<std::uint32_t>(member.size() - 1);
        b.out.bindReference(slot, owner, item.member, index, at, name,
                            std::is_void_v<N> ? kNoNarrow : b.tables.narrowOf[item.action]);
      });
    } else {
      using H = typename written_as<typename F::member, X, Src>::type;
      static_assert(!std::is_void_v<H>, "no alternative of the member takes the value written");
      read_value<Src, H>(b, b.payload(item.record), [&](auto&& value) {
        static_assert(!std::is_same_v<H, std::string_view> ||
                      !std::is_same_v<std::remove_cvref_t<decltype(value)>, std::string>,
                      "a converter returning std::string needs an owning AST property");
        detail::place<H>(member, std::forward<decltype(value)>(value));
      });
    }
    return true;
  }
};

// What a node of the class that action `C` of the rows builds or adopts takes of
// the values from `first` up: the writes `Ws`, those of its members and its
// bases', each applied in place; any other value goes through the write of
// its own action, which says whether the node can take it.
template <class Does, std::uint16_t C, std::uint16_t... Ws>
void fill_class(AstBuilder& b, AstNode* owner, Pending* first) {
  using Rows = typename Does::rows;
  using T = typename detail::tl_at_t<C, Rows>::type;
  [[maybe_unused]] T* const node = detail::ast_cast_unchecked<T>(owner);
  const Pending* const top = b.top();
  for (const Pending* item = first; item != top;) {
    const auto count = std::min<std::ptrdiff_t>(top - item, AstBuilder::kStopRecords);
    const Pending* const end = item + count;
    for (; item != end; ++item) {
      const std::uint16_t action = item->action;
      bool taken = true;
      if (((action == Ws ? (taken = RowWrite<detail::tl_at_t<Ws, Rows>>::into(b, node, *item), true)
                         : false) ||
           ...)) {
        if (!taken) b.broken(item->record);
        continue;
      }
      // A node nothing named is left out of the tree.
      if (action != kNoAction && !b.tables.writes[action](b, owner, *item)) b.broken(item->record);
    }
    if (item != top) [[unlikely]] throwIfCancelled(b.stop);
  }
  b.drop(first);
}

// The steps of a record: its actions `Is` of the rows, innermost first.
template <class Does, std::uint16_t... Is>
struct Steps {
  using Rows = typename Does::rows;
  static constexpr std::size_t count = sizeof...(Is);
  static constexpr std::uint16_t action[] = {Is...};
  static constexpr WriteKind kind[] = {detail::tl_at_t<Is, Rows>::kind...};
  static constexpr bool node[] = {detail::tl_at_t<Is, Rows>::node...};

  static constexpr bool builds(std::size_t p) {
    return kind[p] == WriteKind::create || kind[p] == WriteKind::climb;
  }
  // Per step: `holds`, a node a step before it built, nested or adopted is
  // in hand and no member took it since; `taker`, the step that builds the
  // node its value goes into, or `count` where the value waits.
  struct Plan {
    bool holds[count];
    std::size_t taker[count];
  };
  static constexpr Plan plan = [] {
    Plan out{};
    for (std::size_t p = 1; p < count; ++p) {
      const WriteKind k = kind[p - 1];
      out.holds[p] = builds(p - 1) || k == WriteKind::close || k == WriteKind::nest ||
                     (!node[p - 1] && out.holds[p - 1]);
    }
    for (std::size_t p = count; p-- > 0;) {
      const bool value = kind[p] == WriteKind::flag
                             ? !out.holds[p]
                             : (kind[p] == WriteKind::assign || kind[p] == WriteKind::append) &&
                                   !node[p];
      if (!value || p + 1 == count)
        out.taker[p] = count;
      else
        out.taker[p] = builds(p + 1) ? p + 1 : out.taker[p + 1];
    }
    return out;
  }();
};

// The value step `J` of a record reads, written into the node `made` that
// its step `P` builds.
template <class Does, std::size_t J, std::size_t P, class T, std::uint16_t... Is>
CURSIV_ALWAYS_INLINE inline void write_own(AstBuilder& b, NodeId id, T* made) {
  using Rows = typename Does::rows;
  using S = Steps<Does, Is...>;
  if constexpr (S::plan.taker[J] == P) {
    constexpr std::uint16_t I = S::action[J];
    using W = RowWrite<detail::tl_at_t<I, Rows>>;
    if constexpr (std::is_base_of_v<typename W::F::owner, T>) {
      if (!W::put(b, made, Pending{id, I, Does::member[I], nullptr})) b.broken(id);
    } else {
      b.broken(id);
    }
  }
}

// Step `P` of a record of a language whose tables `Does` names; `held` is
// the node the record has in hand, and `written` says whether a member took
// it.
template <class Does, std::size_t P, std::uint16_t... Is>
CURSIV_ALWAYS_INLINE inline void apply(AstBuilder& b, NodeId id, const CstNode& node,
                                       AstNode*& held, bool& written) {
  using Rows = typename Does::rows;
  using S = Steps<Does, Is...>;
  constexpr std::uint16_t I = S::action[P];
  using R = detail::tl_at_t<I, Rows>;
  constexpr WriteKind kind = S::kind[P];
  [[maybe_unused]] constexpr std::uint16_t M = Does::member[I];
  if constexpr (S::builds(P)) {
    using T = typename R::type;
    T* const made = b.out.createNode<T>(id);
    held = made;
    written = false;
    if (Pending* const first = b.inside(id, node); first != b.top())
      Does::template fill<I>(b, held, first);
    [&]<std::size_t... Js>(std::index_sequence<Js...>) {
      (write_own<Does, Js, P, T, Is...>(b, id, made), ...);
    }(std::make_index_sequence<P>{});
  } else if constexpr (kind == WriteKind::close) {
    if (held == nullptr) {
      Pending* const first = b.inside(id, node);
      held = b.adopt(&kAstTypeTag<typename R::type>, first, id);
      written = false;
      Does::template fill<I>(b, held, first);
    }
  } else if constexpr (S::plan.taker[P] != S::count) {
    // Written by the step that builds its node.
  } else if constexpr (kind == WriteKind::assign || kind == WriteKind::append) {
    if constexpr (R::node) {
      if (held == nullptr) {
        held = b.take(&RowWrite<R>::takes, id, node);
        if (held == nullptr) b.broken(id);
      }
      b.push({id, I, M, held});
      written = true;
    } else {
      b.push({id, I, M, nullptr});
    }
  } else if constexpr (kind == WriteKind::flag) {
    // Written at once to the record's own node: left waiting, it would go to
    // the node the region above opens. A node a member took is not the
    // record's any more, and the flag waits for its owner.
    if constexpr (!S::plan.holds[P])
      b.push({id, I, M, nullptr});
    else if (held == nullptr || !RowWrite<R>::run(b, held, Pending{id, I, M, nullptr}))
      b.broken(id);
  } else if constexpr (kind == WriteKind::nest) {
    using T = typename R::type;
    if (node.subtreeSize == 1) b.broken(id);
    Pending* const first = b.inside(id, node);
    Pending* const own = b.inside(id - 1);
    AstNode* const inner = b.wrapped(first, own, id);
    T* const made = b.out.createNode<T>(id);
    held = made;
    written = false;
    if (!RowWrite<R>::put(b, made, Pending{id, I, M, inner})) b.broken(id);
    if (own != b.top()) Does::template fill<I>(b, held, own);
    b.drop(first);
  }
}

// What the record of an element does: its actions `Is` of the rows,
// innermost first, by `Does::member` and `Does::fill`. The node it holds
// then waits for the record above: as `write` writes it, where given.
template <class Does, std::uint16_t... Is>
void record(AstBuilder& b, NodeId id, const CstNode& node, DeferredWrite write) {
  AstNode* held = nullptr;
  bool written = false;
  [&]<std::size_t... Ps>(std::index_sequence<Ps...>) {
    (apply<Does, Ps, Is...>(b, id, node, held, written), ...);
  }(std::make_index_sequence<sizeof...(Is)>{});
  if (held != nullptr && (write.action != kNoAction || !written))
    b.push({id, write.action, write.member, held});
}

template <class Rows>
struct AstWritesOf;
template <class... Rs>
struct AstWritesOf<TypeList<Rs...>> {
  // One more than the rows: no array holds nothing.
  static constexpr AstWrite kAll[sizeof...(Rs) + 1] = {RowWrite<Rs>::run...};
};

}  // namespace cursiv::engine
