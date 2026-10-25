// The directives of one compiled grammar, applied: `create` and `nest` name
// a class, an assignment names a member and the payload's own converter.
// What it does not model, it refuses: a `Parts` converter, a payload with
// several sources, a member no `put` expresses.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/grammar/PayloadOf.hpp"
#include "cursiv/core/grammar/expr.hpp"
#include "cursiv/core/syntax-tree/AstArena.hpp"
#include "cursiv/core/syntax-tree/AstField.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/VariantNodeAlt.hpp"
#include "cursiv/core/utils/typelist.hpp"
#include "cursiv/fuzz/ArenaResource.hpp"
#include "cursiv/fuzz/DeclaredModel.hpp"
#include "cursiv/fuzz/GrammarActions.hpp"

namespace cursiv::fuzzer {

template <class T>
[[nodiscard]] inline bool node_is(const AstNode* n) {
  return n != nullptr && n->type != nullptr &&
         n->type->derivesFrom(&ast_type_tag<T>);
}

// Rendered as text, not bytes, so the two trees are weighed on one scale
// and a divergence reads. `%.17g` round-trips a double, so equal doubles
// print equal.

template <class X>
struct Render {
  static bool run(const X& v, std::string& out) {
    if constexpr (std::is_same_v<X, std::string_view>) {
      out.assign(v);
      return true;
    } else if constexpr (std::is_same_v<X, bool>) {
      out = v ? "true" : "false";
      return true;
    } else if constexpr (std::is_same_v<X, std::monostate>) {
      out = "-";
      return true;
    } else if constexpr (std::is_enum_v<X>) {
      out = std::to_string(static_cast<long long>(v));
      return true;
    } else if constexpr (std::is_floating_point_v<X>) {
      char buf[40];
      const int n =
          std::snprintf(buf, sizeof buf, "%.17g", static_cast<double>(v));
      out.assign(buf, n > 0 ? static_cast<std::size_t>(n) : 0u);
      return true;
    } else if constexpr (std::is_integral_v<X>) {
      out = std::to_string(v);
      return true;
    } else {
      return false;
    }
  }
};

template <class... As>
struct Render<std::variant<As...>> {
  static bool run(const std::variant<As...>& v, std::string& out) {
    out = "#" + std::to_string(v.index()) + ":";
    bool ok = true;
    std::visit(
        [&](const auto& alt) {
          using A = std::remove_cvref_t<decltype(alt)>;
          if constexpr (detail::ast_pointer_alt<A>) {
            ok = false;  // a node in a variant: no reader reaches it
          } else {
            std::string s;
            ok = Render<A>::run(alt, s);
            out += s;
          }
        },
        v);
    return ok;
  }
};

template <class L>
struct only_one {
  using type = void;
};
template <class T>
struct only_one<TypeList<T>> {
  using type = T;
};

// The alternative a variant takes: the one whose type is what the payload
// produced, nothing else -- a value never lands in a slot by conversion.
template <class Held, class V, bool = detail::is_variant_v<Held>>
struct alt_of {
  using type = Held;
};
template <class... As, class V>
struct alt_of<std::variant<As...>, V, true> {
  using type = typename only_one<typename detail::tl_cat<detail::cond_t<
      std::is_same_v<As, V>, TypeList<As>, TypeList<>>...>::type>::type;
};

// A converter that answers a positioned result may refuse; the engine writes
// nothing then, and neither does this.
template <class T>
inline constexpr bool is_conv_result = false;
template <class T>
inline constexpr bool is_conv_result<ConversionResult<T>> = true;

// The single alternative of a variant that holds a node.
template <class Held>
using node_alt_t =
    typename only_one<typename detail::variant_node_alt<Held>::type>::type;

template <class Held, class A, class V>
bool put(Held& slot, V&& v) {
  if constexpr (detail::is_variant_v<Held>) {
    if constexpr (std::is_void_v<A>) {
      return false;
    } else {
      slot.template emplace<A>(A(std::forward<V>(v)));
      return true;
    }
  } else if constexpr (requires { Held(std::forward<V>(v)); }) {
    slot = Held(std::forward<V>(v));
    return true;
  } else {
    return false;
  }
}

template <class F, class A, class V>
bool store(typename F::owner& o, AstArena& ar, V&& v) {
  using Held = typename F::held;
  if constexpr (F::storage == detail::FieldStorage::vector) {
    Held slot{};
    if (!put<Held, A>(slot, std::forward<V>(v))) return false;
    (o.*F::ptr).push_back(ar, slot);
    return true;
  } else if constexpr (F::storage == detail::FieldStorage::optional) {
    Held slot{};
    if (!put<Held, A>(slot, std::forward<V>(v))) return false;
    o.*F::ptr = slot;
    return true;
  } else {
    return put<Held, A>(o.*F::ptr, std::forward<V>(v));
  }
}

template <class E>
struct made_type {
  using type = void;
};
template <class T, class E>
struct made_type<Create<T, E>> {
  using type = T;
};
template <class T, auto P, class E>
struct made_type<Nest<T, P, E>> {
  using type = T;
};

// The member an assignment names, and the payload it writes.
template <class E>
struct directive {
  static constexpr bool is_one = false;
};
template <auto Sp, class E>
struct directive<Assign<Sp, E>> {
  static constexpr bool is_one = true;
  static constexpr bool enable = false;
  static constexpr auto ptr = spec_ptr<Sp>;
  using payload = E;
};
template <auto Sp, class E>
struct directive<Append<Sp, E>> {
  static constexpr bool is_one = true;
  static constexpr bool enable = false;
  static constexpr auto ptr = spec_ptr<Sp>;
  using payload = E;
};
template <auto P, class E>
struct directive<Flag<P, E>> {
  static constexpr bool is_one = true;
  static constexpr bool enable = true;
  static constexpr auto ptr = P;
  using payload = E;
};
template <class T, auto P, class E>
struct directive<Nest<T, P, E>> {
  static constexpr bool is_one = true;
  static constexpr bool enable = false;
  static constexpr auto ptr = P;
  using payload = E;
};

using DenoteFn = std::string_view (*)(std::string_view);
using MakeFn = AstNode* (*)(AstArena&);
using NodeFn = bool (*)(AstNode&, AstNode*, AstArena&);
using TextFn = bool (*)(AstNode&, ElementId, std::string_view, AstArena&);

template <class T>
AstNode* make_node(AstArena& ar) {
  return ar.make<T>();
}

template <class R>
std::string_view denote_of(std::string_view raw) {
  return detail::conv_t<R>::convert(raw);
}

// A converter that answers a view is what a leaf denotes; anything else
// leaves the raw text standing.
template <class E>
consteval DenoteFn denote_entry() {
  using T = std::remove_cvref_t<E>;
  if constexpr (requires { typename T::kind; })
    if constexpr (is_terminal_rule<T> || is_data_rule<T>)
      if constexpr (detail::ConvertsText<detail::conv_t<T>>)
        if constexpr (std::is_same_v<std::remove_cvref_t<
                                         decltype(detail::conv_t<T>::convert(
                                             std::string_view{}))>,
                                     std::string_view>)
          return &denote_of<T>;
  return nullptr;
}

template <class E>
bool write_node_of(AstNode& owner, AstNode* child, AstArena& ar) {
  using D = directive<E>;
  using F = detail::field_tag<D::ptr>;
  if (!node_is<typename F::owner>(&owner)) return false;
  auto& o = static_cast<typename F::owner&>(owner);
  using Held = typename F::held;
  if constexpr (F::nature == detail::FieldNature::node) {
    using T = std::remove_pointer_t<Held>;
    if (!node_is<T>(child)) return false;
    return store<F, Held>(o, ar, static_cast<Held>(child));
  } else if constexpr (F::nature == detail::FieldNature::variant) {
    using A = node_alt_t<Held>;
    if constexpr (std::is_void_v<A>) {
      return false;
    } else {
      using T = std::remove_pointer_t<A>;
      if (!node_is<T>(child)) return false;
      return store<F, A>(o, ar, static_cast<A>(child));
    }
  } else {
    return false;
  }
}

// The converter's answer. A positioned result that failed is still
// modelled: the engine leaves the member at its default and says where,
// so both trees agree -- refusing here would excuse a member the walk did
// read.
template <class F, class A, class Res>
bool accept(typename F::owner& o, AstArena& ar, Res&& r) {
  using R = std::remove_cvref_t<Res>;
  if constexpr (is_conv_result<R>) {
    if (!r) return true;
    return store<F, A>(o, ar, std::move(*r.value));
  } else {
    return store<F, A>(o, ar, std::forward<Res>(r));
  }
}

// The row a payload rule is named by; `kNoElement` for a bare matcher, which
// is the fallback tried when no rule of the choice matched.
template <class C, class R>
consteval ElementId payload_row() {
  if constexpr (std::is_void_v<R>)
    return kNoElement;
  else
    return model::call_row<C, R>;
}

// The rule a payload resolves to. An override keeps the base's name at the
// call site but replaces what it matches, so the converter and value type
// belong to the derived rule. A bare matcher names no rule and stands for
// itself.
template <class R>
concept declares_kind = requires { typename R::kind; };
template <class C, class R, bool = declares_kind<R>>
struct pay_rule {
  using type = R;
};
template <class C, class R>
struct pay_rule<C, R, true> {
  using resolved = typename cursiv::Normalize<C>::template rule<R>;
  using type = std::conditional_t<std::is_void_v<resolved>, R, resolved>;
};

// One source of one directive: the value is what the rule produces, and
// the member takes it through its own alternative when it is a variant.
template <class C, class F, class S>
bool one_text([[maybe_unused]] typename F::owner& o,
              [[maybe_unused]] ElementId payload,
              [[maybe_unused]] bool fallback,
              [[maybe_unused]] std::string_view text,
              [[maybe_unused]] AstArena& ar) {
  if constexpr (!detail::is_text_pay<S>) {
    return false;
  } else {
    using R = typename S::rule;
    constexpr ElementId kRow = payload_row<C, R>();
    if (fallback ? kRow != kNoElement : kRow != payload) return false;
    // The name picks the source; the resolved rule converts it.
    using RR = typename pay_rule<C, R>::type;
    using V = std::remove_cvref_t<typename detail::text_value<RR>::type>;
    using A = typename alt_of<typename F::held, V>::type;
    using CR = detail::converter_rule_t<RR>;
    if constexpr (!std::is_void_v<CR>) {
      // The form the deduction retained, in the grammar's own order.
      if constexpr (detail::ConvertsTextMem<CR>) {
        ArenaResource res(ar);
        return accept<F, A>(o, ar, CR::convert(text, &res));
      } else if constexpr (detail::ConvertsText<CR>) {
        return accept<F, A>(o, ar, CR::convert(text));
      } else {
        return false;  // a converter reading `Parts` is not modelled
      }
    } else if constexpr (std::is_void_v<A> || !has_default_convert<A>) {
      return false;
    } else {
      return accept<F, A>(o, ar, default_convert<A>(text));
    }
  }
}

template <class C, class F, class L>
struct text_sources;
template <class C, class F, class... Ss>
struct text_sources<C, F, TypeList<Ss...>> {
  static bool run([[maybe_unused]] typename F::owner& o,
                  [[maybe_unused]] ElementId payload,
                  [[maybe_unused]] std::string_view text,
                  [[maybe_unused]] AstArena& ar) {
    bool done = false;
    // The rule that spelled it first, the bare matcher only if none did.
    ((done = done || one_text<C, F, Ss>(o, payload, false, text, ar)), ...);
    ((done = done || one_text<C, F, Ss>(o, payload, true, text, ar)), ...);
    return done;
  }
};

template <class C, class E>
bool write_text_of(AstNode& owner, [[maybe_unused]] ElementId payload,
                   [[maybe_unused]] std::string_view text, AstArena& ar) {
  using D = directive<E>;
  using F = detail::field_tag<D::ptr>;
  if (!node_is<typename F::owner>(&owner)) return false;
  auto& o = static_cast<typename F::owner&>(owner);

  if constexpr (D::enable) {
    return store<F, typename F::held>(o, ar, true);
  } else if constexpr (detail::is_ref_nature(F::nature)) {
    // A reference is judged on its text: the name is what the document
    // spells, and resolving it is the linker's verdict, not the walk's.
    typename F::held slot{};
    slot.refText = text;
    return store<F, typename F::held>(o, ar, slot);
  } else {
    return text_sources<
        C, F,
        typename detail::payload_of<typename D::payload>::type>::run(o, payload,
                                                                     text, ar);
  }
}

// Whether the rule builds its own node: true when a root `create` wraps
// its body, false when its alternatives each build their own head and the
// rule just adopts what they built.
template <class E>
inline constexpr bool root_creates = false;
template <class T, class E>
inline constexpr bool root_creates<Create<T, E>> = true;
template <class C, class R>
inline constexpr bool rule_creates = root_creates<
    cursiv::declared_body_t<typename cursiv::Normalize<C>::template rule<R>>>;

template <class C, class E>
consteval MakeFn make_entry() {
  using T = std::remove_cvref_t<E>;
  using Made = typename made_type<T>::type;
  if constexpr (!std::is_void_v<Made>) {
    return &make_node<Made>;
  } else if constexpr (requires { typename T::kind; }) {
    // A climb builds one node per fold; a rule whose alternatives build no
    // head node carries the creation, its own element making it.
    if constexpr (is_infix_rule<T>)
      return &make_node<rule_type_t<T>>;
    else if constexpr (is_node_rule<T> && rule_creates<C, T>)
      return &make_node<rule_type_t<T>>;
    else
      return nullptr;
  } else {
    return nullptr;
  }
}

// The operand rule's row. `Ref<R>` resolves to R's own row, which is where an
// effective rule and the name it was declared under meet.
template <class C, class R>
consteval ElementId primary_row() {
  return model::call_row<C, typename R::primary>;
}

// Levels are declared tightest-first; the step a resolution compares is
// `N - I` for level `I` of `N`.
template <class C, class L, std::uint8_t Prec, bool Right>
struct ops_of_level;
template <class C, class... As, std::uint8_t Prec, bool Right>
struct ops_of_level<C, TypeList<As...>, Prec, Right> {
  static constexpr std::size_t size = sizeof...(As);
  static constexpr std::array<InfixOp, sizeof...(As)> value = {
      InfixOp{model::op_row<C, As>, Prec, Right}...};
};

// The operators as declared: substituting the word boundary into a `kw<>`
// guard is the lowering's reading of them, not the author's.
template <class Lvls, std::size_t I>
using level_alts_t =
    typename detail::alts_list<typename Lvls::template at<I>::op_expr>::type;

template <class C, class R, class Is>
struct infix_ops;
template <class C, class R, std::size_t... Is>
struct infix_ops<C, R, std::index_sequence<Is...>> {
  using Lvls = typename R::levels;
  template <std::size_t I>
  using Level =
      ops_of_level<C, level_alts_t<Lvls, I>,
                   static_cast<std::uint8_t>(sizeof...(Is) - I),
                   Lvls::template at<I>::kGroup == Association::right>;
  static constexpr std::size_t kN = (std::size_t{0} + ... + Level<Is>::size);
  static constexpr std::array<InfixOp, kN> kOps = []() consteval {
    std::array<InfixOp, kN> out{};
    std::size_t at = 0;
    (([&] {
       for (const InfixOp& o : Level<Is>::value) out[at++] = o;
     }()),
     ...);
    return out;
  }();
};

template <class C, class R>
using infix_ops_t = infix_ops<C, R, std::make_index_sequence<R::levels::size>>;

// What an operator hands the member it writes: the constant its own
// converter names, or the text it matched, its parts joined, where it names
// none.
template <class Op>
constexpr auto op_value(std::string_view text) {
  using Rule = typename model::op_position<Op>::type;
  if constexpr (requires { typename Rule::slot; }) {
    using Conv = detail::conv_t<Rule>;
    if constexpr (detail::ConvertsNothing<Conv>)
      return Conv::convert();
    else if constexpr (detail::ConvertsText<Conv>)
      return Conv::convert(text);
    else
      return text;
  } else {
    return text;
  }
}

// The operators of every level, flattened: the fold below asks each in turn
// which one the generator chose.
template <class Lvls, class Is>
struct level_alts_all;
template <class Lvls, std::size_t... Is>
struct level_alts_all<Lvls, std::index_sequence<Is...>>
    : detail::tl_cat<level_alts_t<Lvls, Is>...> {};

// The three writes a fold commits, spelled from the rule's own pointers. The
// operator is named by its row, not by its text: two spellings may denote one
// constant, and a constant is what the member holds.
template <class C, class R, class N, class... Ops>
bool write_op(ElementId chosen, N& n, std::string_view text, TypeList<Ops...>) {
  bool done = false;
  (((!done && model::op_row<C, Ops> == chosen)
        ? (n.*R::op_pointer = op_value<Ops>(text), done = true)
        : false),
   ...);
  return done;
}

template <class C, class R>
bool fold_of(ElementId op, AstNode& node, AstNode* lhs, std::string_view text,
             AstNode* rhs) {
  using N = typename R::node_type;
  using LT = std::remove_pointer_t<typename member_traits<
      std::remove_cv_t<decltype(R::left_pointer)>>::type>;
  using RT = std::remove_pointer_t<typename member_traits<
      std::remove_cv_t<decltype(R::right_pointer)>>::type>;
  if (!node_is<N>(&node) || !node_is<LT>(lhs) || !node_is<RT>(rhs))
    return false;
  N& n = static_cast<N&>(node);
  n.*R::left_pointer = static_cast<LT*>(lhs);
  using Alts = typename level_alts_all<
      typename R::levels,
      std::make_index_sequence<R::levels::size>>::type;
  if (!write_op<C, R>(op, n, text, Alts{})) return false;
  n.*R::right_pointer = static_cast<RT*>(rhs);
  return true;
}

using FoldFn = bool (*)(ElementId, AstNode&, AstNode*, std::string_view,
                        AstNode*);

template <class C, class E>
consteval ElementId primary_entry() {
  using T = std::remove_cvref_t<E>;
  if constexpr (requires { typename T::kind; }) {
    if constexpr (is_infix_rule<T>) return primary_row<C, T>();
  }
  return kNoElement;
}

template <class C, class E>
consteval std::span<const InfixOp> ops_entry() {
  using T = std::remove_cvref_t<E>;
  if constexpr (requires { typename T::kind; }) {
    if constexpr (is_infix_rule<T>)
      return std::span<const InfixOp>(infix_ops_t<C, T>::kOps);
  }
  return {};
}

template <class C, class E>
consteval FoldFn fold_entry() {
  using T = std::remove_cvref_t<E>;
  if constexpr (requires { typename T::kind; }) {
    if constexpr (is_infix_rule<T>) return &fold_of<C, T>;
  }
  return nullptr;
}

template <class E>
consteval NodeFn node_entry() {
  using T = std::remove_cvref_t<E>;
  if constexpr (directive<T>::is_one) return &write_node_of<T>;
  return nullptr;
}

template <class C, class E>
consteval TextFn text_entry() {
  using T = std::remove_cvref_t<E>;
  if constexpr (directive<T>::is_one) return &write_text_of<C, T>;
  return nullptr;
}

// The appended rows count too: a directive the table spread over a choice
// kept no row of its own, and the children column names the appended
// spelling -- a table stopping at `element_list` would refuse every such
// write.
template <class C, class L, class X>
struct act_rows;
template <class C, class... Es, class... Xs>
struct act_rows<C, TypeList<Es...>, TypeList<Xs...>> {
  static constexpr std::size_t kN = sizeof...(Es) + 2 + sizeof...(Xs);
  static constexpr std::array<MakeFn, kN> kMake = {
      make_entry<C, Es>()..., nullptr, nullptr, make_entry<C, Xs>()...};
  static constexpr std::array<NodeFn, kN> kNode = {
      node_entry<Es>()..., nullptr, nullptr, node_entry<Xs>()...};
  static constexpr std::array<TextFn, kN> kText = {
      text_entry<C, Es>()..., nullptr, nullptr, text_entry<C, Xs>()...};
  static constexpr std::array<ElementId, kN> kPrimary = {
      primary_entry<C, Es>()..., kNoElement, kNoElement,
      primary_entry<C, Xs>()...};
  static constexpr std::array<std::span<const InfixOp>, kN> kOps = {
      ops_entry<C, Es>()..., std::span<const InfixOp>{},
      std::span<const InfixOp>{}, ops_entry<C, Xs>()...};
  static constexpr std::array<FoldFn, kN> kFold = {
      fold_entry<C, Es>()..., nullptr, nullptr, fold_entry<C, Xs>()...};
  static constexpr std::array<DenoteFn, kN> kDenote = {
      denote_entry<Es>()..., nullptr, nullptr, denote_entry<Xs>()...};
};

template <class E>
struct field_of {
  using type = TypeList<>;
};
template <auto Sp, class E>
struct field_of<Assign<Sp, E>> {
  using type = TypeList<detail::field_tag<spec_ptr<Sp>>>;
};
template <auto Sp, class E>
struct field_of<Append<Sp, E>> {
  using type = TypeList<detail::field_tag<spec_ptr<Sp>>>;
};
template <auto P, class E>
struct field_of<Flag<P, E>> {
  using type = TypeList<detail::field_tag<P>>;
};
template <class T, auto P, class E>
struct field_of<Nest<T, P, E>> {
  using type = TypeList<detail::field_tag<P>>;
};

// A climb's three members are named by the rule, not by a directive.
template <class E>
  requires requires { typename E::kind; } && is_infix_rule<E>
struct field_of<E> {
  using type = TypeList<detail::field_tag<E::left_pointer>,
                        detail::field_tag<E::op_pointer>,
                        detail::field_tag<E::right_pointer>>;
};

template <class L>
struct fields_of;
template <class... Es>
struct fields_of<TypeList<Es...>>
    : detail::dedupe<detail::tl_cat_t<
          typename field_of<std::remove_cvref_t<Es>>::type...>> {};

// Always empty: distributing a directive over a choice is the lowering's
// job, not the declared model's, and the walk below reaches the payload on
// its own.
using spread_rows = TypeList<>;

using ValueYield = cursiv::function_ref<void(std::string_view, std::uint32_t,
                                             ValueKind, std::string_view)>;

template <class F, class X>
void yield_one(std::string_view name, std::uint32_t index, const X& v,
               ValueYield yield) {
  if constexpr (detail::is_ref_nature(F::nature)) {
    if constexpr (F::nature == detail::FieldNature::reference)
      yield(name, index, ValueKind::text, v.refText);
    else
      yield(name, index, ValueKind::unreadable, {});
  } else {
    std::string out;
    if (Render<X>::run(v, out))
      yield(name, index, ValueKind::text, out);
    else
      yield(name, index, ValueKind::unreadable, out);
  }
}

template <class F>
void yield_field(const AstNode& n, ValueYield yield) {
  if constexpr (F::nature == detail::FieldNature::node) {
    return;  // `AstReflection` walks the nodes
  } else {
    if (!node_is<typename F::owner>(&n)) return;
    const auto& o = static_cast<const typename F::owner&>(n);
    constexpr std::uint32_t kNone = ~std::uint32_t{0};
    if constexpr (F::storage == detail::FieldStorage::vector) {
      std::uint32_t at = 0;
      for (const auto& v : o.*F::ptr) yield_one<F>(F::name, at++, v, yield);
    } else if constexpr (F::storage == detail::FieldStorage::optional) {
      if ((o.*F::ptr).has_value())
        yield_one<F>(F::name, kNone, *(o.*F::ptr), yield);
      else
        yield(F::name, kNone, ValueKind::text, "(unset)");
    } else {
      yield_one<F>(F::name, kNone, o.*F::ptr, yield);
    }
  }
}

template <class L>
struct value_walk;
template <class... Fs>
struct value_walk<TypeList<Fs...>> {
  static void run(const AstNode& n, ValueYield yield) {
    (yield_field<Fs>(n, yield), ...);
  }
};

template <class C>
struct CursivActions final : GrammarActions {
  using Rows =
      act_rows<C, typename model::positions_for<C>::type, spread_rows>;
  using Fields =
      typename fields_of<typename model::positions_for<C>::type>::type;

  [[nodiscard]] AstNode* make(ElementId e, AstArena& ar) const override {
    if (e >= Rows::kMake.size() || Rows::kMake[e] == nullptr) return nullptr;
    return Rows::kMake[e](ar);
  }

  [[nodiscard]] ElementId primary(ElementId e) const override {
    return e < Rows::kPrimary.size() ? Rows::kPrimary[e] : kNoElement;
  }

  [[nodiscard]] std::span<const InfixOp> operators(ElementId e) const override {
    return e < Rows::kOps.size() ? Rows::kOps[e] : std::span<const InfixOp>{};
  }

  bool fold(ElementId e, ElementId op, AstNode& node, AstNode* left,
            std::string_view text, AstNode* right) const override {
    if (e >= Rows::kFold.size() || Rows::kFold[e] == nullptr) return false;
    return Rows::kFold[e](op, node, left, text, right);
  }

  bool write_node(ElementId e, AstNode& owner, AstNode* child,
                  AstArena& ar) const override {
    if (e >= Rows::kNode.size() || Rows::kNode[e] == nullptr) return false;
    return Rows::kNode[e](owner, child, ar);
  }

  bool write_text(ElementId e, ElementId payload, AstNode& owner,
                  std::string_view text, AstArena& ar) const override {
    if (e >= Rows::kText.size() || Rows::kText[e] == nullptr) return false;
    return Rows::kText[e](owner, payload, text, ar);
  }

  [[nodiscard]] std::string_view denote(ElementId e,
                                        std::string_view text) const override {
    if (e >= Rows::kDenote.size() || Rows::kDenote[e] == nullptr) return text;
    return Rows::kDenote[e](text);
  }

  void visitValues(const AstNode& n, ValueYield yield) const override {
    value_walk<Fields>::run(n, yield);
  }
};

}  // namespace cursiv::fuzzer
