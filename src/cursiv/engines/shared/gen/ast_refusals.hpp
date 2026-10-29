#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>
#include <variant>

#include "cursiv/core/grammar/Conversion.hpp"
#include "cursiv/core/grammar/ExprShape.hpp"
#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/grammar/PayloadOf.hpp"
#include "cursiv/core/grammar/expr.hpp"
#include "cursiv/core/syntax-tree/AstField.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/MemberTraits.hpp"
#include "cursiv/core/syntax-tree/VariantNodeAlt.hpp"
#include "cursiv/core/utils/TypeName.hpp"
#include "cursiv/core/utils/ctstring.hpp"
#include "cursiv/core/utils/typelist.hpp"
#include "cursiv/engines/shared/gen/GrammarRefusals.hpp"
#include "cursiv/engines/shared/runtime/ast_fields.hpp"

// Why a grammar is refused for the shape of its AST: one static_assert per
// mistake a member and its payload can make, each guarded by the case it
// diagnoses, so exactly one fires and the message names the actual mistake.

namespace cursiv::detail {

// How many nodes of a member's family one match of a shape leaves for the
// directive above it: what that directive's node path takes when the shape
// pays no value of its own.

// The builder takes the first node of the family and ignores the others, so
// a node of another class is no node here. One that a rule of a base class
// builds may or may not belong: it counts in `hi` and not in `lo`.
template <class T, class L>
struct family_fit;
template <class T, class... Gs>
struct family_fit<T, TypeList<Gs...>> {
  static constexpr bool surely = (std::is_base_of_v<Gs, T> || ...);
  static constexpr bool maybe = surely || (std::is_base_of_v<T, Gs> || ...);
};

// A range capped at 2, and whether a repetition can hand over more than one.
consteval unsigned nodes_cap(std::size_t n) {
  return n > 2 ? 2u : static_cast<unsigned>(n);
}
consteval unsigned nodes_times(std::size_t a, unsigned b) {
  return a == 0 || b == 0 ? 0u : nodes_cap(a > 2 ? 2 : a * b);
}
template <class L, class E>
struct nodes_held {
  static constexpr unsigned lo = 0, hi = 0;
  static constexpr bool repeated = false;
};
template <class L, class T>
struct nodes_held_of {
  static constexpr unsigned lo = family_fit<T, L>::surely ? 1 : 0,
                            hi = family_fit<T, L>::maybe ? 1 : 0;
  static constexpr bool repeated = false;
};
template <class L, class R>
  requires(is_node_rule<named_rule_t<R>> || is_infix_rule<named_rule_t<R>>)
struct nodes_held<L, Ref<R>> : nodes_held_of<L, rule_type_t<named_rule_t<R>>> {
};
template <class L, class T, class E>
struct nodes_held<L, Create<T, E>> : nodes_held_of<L, T> {};
template <class L, class... Es>
struct nodes_held<L, Seq<Es...>> {
  static constexpr unsigned lo = nodes_cap((0u + ... + nodes_held<L, Es>::lo)),
                            hi = nodes_cap((0u + ... + nodes_held<L, Es>::hi));
  static constexpr bool repeated = (nodes_held<L, Es>::repeated || ...);
};
template <class L, class... Es>
struct nodes_held<L, Unordered<Es...>> : nodes_held<L, Seq<Es...>> {};
template <class L, class E0, class... Es>
struct nodes_held<L, Choice<E0, Es...>> {
  static constexpr unsigned lo =
      std::min({nodes_held<L, E0>::lo, nodes_held<L, Es>::lo...});
  static constexpr unsigned hi =
      std::max({nodes_held<L, E0>::hi, nodes_held<L, Es>::hi...});
  static constexpr bool repeated =
      nodes_held<L, E0>::repeated || (nodes_held<L, Es>::repeated || ...);
};
template <class L, std::size_t Mn, std::size_t Mx, class E>
struct nodes_held<L, Repeat<Mn, Mx, E>> {
  static constexpr unsigned lo = nodes_times(Mn, nodes_held<L, E>::lo),
                            hi = nodes_times(Mx, nodes_held<L, E>::hi);
  static constexpr bool repeated =
      nodes_held<L, E>::repeated || (Mx > 1 && nodes_held<L, E>::hi != 0);
};
template <class L, class E, str T>
struct nodes_held<L, Doc<E, T>> : nodes_held<L, E> {};
template <class L, class I, class H, class E>
struct nodes_held<L, Skip<I, H, E>> : nodes_held<L, E> {};
template <class L, class G, class E>
struct nodes_held<L, Embed<G, E>> : nodes_held<L, E> {};

// The absent arm of an at-most-once repetition writes nothing, so the item
// alone is counted.
template <class E>
struct once_item {
  using type = E;
};
template <std::size_t Mn, class E>
struct once_item<Repeat<Mn, 1, E>> : once_item<E> {};

template <class T, class... Us>
consteval bool bases_stated_of(TypeList<Us...>) {
  return (::AstBase<T, Us>() && ...);
}
template <class... Ts>
consteval bool bases_stated(TypeList<Ts...> all) {
  return (bases_stated_of<Ts>(all) && ...);
}

// Whether a member holds an owned text anywhere. Missing an owning variant
// alternative is a wrong program that runs silently: the owning alternative
// of `variant<double, std::string>` is dead, so `double` swallows everything
// and the member keeps its default. Recursion goes through `strip_storage`,
// so a nested holder is reached too.
template <class V>
inline constexpr bool holds_owned_text = is_owned_text<V>;
template <class... As>
inline constexpr bool holds_owned_text<std::variant<As...>> =
    (holds_owned_text<typename strip_storage<As>::type> || ...);

// A member's targets and its node alternatives index each other only if an
// alternative names an AST type exactly when it holds a node: one that names a
// type without holding a node shifts them apart from its position on.
template <class A>
inline constexpr bool alt_names_a_type =
    !std::is_same_v<typename held_targets<A>::type, TypeList<>>;
template <class M>
struct alts_index_targets : std::true_type {};
template <class... As>
struct alts_index_targets<std::variant<As...>>
    : std::bool_constant<((alt_names_a_type<As> == ast_pointer_alt<As>) &&
                          ...)> {};

// One check per (member, payload source).
template <auto P, class Src>
struct check_write {
  using tag = field_tag<P>;
  using X = typename tag::held;
  static constexpr bool ok = admits<X, Src>();
  static constexpr FieldNature nature = tag::nature;
  // A pointer to any class is a node member, so "the pointee is off the
  // AstNode family" and "the payload is the wrong family" are two mistakes.
  static constexpr bool pointee_ok =
      nature != FieldNature::node ||
      std::is_base_of_v<AstNode, std::remove_pointer_t<X>>;
  // A bare `string_view` member refusing its payload is always the dangle:
  // without a converter the law admits every text, so only a materialising
  // converter reaches here -- the generic "no conversion" message below would
  // say the opposite of the truth.
  static constexpr bool view_dangles =
      nature == FieldNature::value && std::is_same_v<X, AstNode::string_view> &&
      is_text_pay<Src> && !ok;
  // A member that owns its text is refused whatever the payload is, so the
  // message is about the member. Asked of the whole held type, a variant
  // alternative owning bytes exactly as a bare member does.
  static constexpr bool owns_text = holds_owned_text<X>;
  // Mixing in a reference alternative shifts the interpreter's positions
  // apart from the writer's, surfacing as `malformed_cst` on every document.
  static constexpr bool variant_alts_index_targets =
      nature != FieldNature::variant || alts_index_targets<X>::value;

  static consteval bool check() {
    static_assert(!owns_text,
                  "an AST member never owns its text: this one holds a "
                  "string, directly or in a variant, and the tree points "
                  "into the document -- make it AstNode::string_view. A "
                  "value that must be built, such as a decoded literal or a "
                  "case-folded name, is allocated by the converter in the "
                  "resource it is given (`static std::string_view "
                  "convert(std::string_view, std::pmr::memory_resource*)`), "
                  "and reaches the member as a view that lives as long as "
                  "the ParseResult");
    static_assert(!(is_ref_nature(nature) && !is_text_pay<Src>),
                  "a reference member takes text: a cross-reference stores "
                  "the name as written, from a terminal or a data rule, never "
                  "a node built by a rule");
    static_assert(!(is_ref_nature(nature) && is_text_pay<Src> && !ok),
                  "a reference member holds its name as a string_view into "
                  "the document, so its value must be a string_view: this "
                  "one is another value (a string, a number, a class), which "
                  "the builder keeps only as a temporary -- give the rule a "
                  "converter that returns a string_view into the input, or "
                  "into the resource the converter is given "
                  "(`convert(std::string_view, "
                  "std::pmr::memory_resource*)`)");
    if constexpr (!pointee_ok)
      ::MemberNodeOutsideAst<P, std::remove_pointer_t<X>>();
    static_assert(!(nature == FieldNature::node && pointee_ok && !ok),
                  "this member holds a node of its own type: the value "
                  "written must be a node whose type derives from the type "
                  "the member points to (from a rule that builds a node, a "
                  "region, or a nest)");
    static_assert(!(nature == FieldNature::variant && !ok),
                  "a variant member takes a value that matches exactly one "
                  "of its alternatives -- this one matches none, or "
                  "several");
    static_assert(variant_alts_index_targets,
                  "a variant member mixes node alternatives with "
                  "alternatives that name an AST type without holding a "
                  "node: a cross-reference, a nested variant, or a pointer to "
                  "a class that does not derive from cursiv::AstNode, so the "
                  "builder would pick the wrong alternative, or none -- give "
                  "the reference a member of its own");
    if constexpr (view_dangles)
      ::BorrowedTextTemporary<P, typename pay_value<Src>::type>();
    // An enum's default conversion matches text against its enumerator names,
    // so a plain text payload already admits; this fires for a node payload or
    // an unbuildable converter result. Whether a literal's spelling is an
    // enumerator is `check_lit_write`'s question.
    static_assert(
        !(nature == FieldNature::value && !ok && !view_dangles && !owns_text),
        "no conversion from this value to the member type: the built-in "
        "conversions cover string_view, bool, arithmetic types and enums, and "
        "nothing else -- give the rule a converter that returns the member's "
        "type: `as<f>(...)` around the rule's body, or a "
        "`cursiv::default_converter<T>` specialisation declaring `static "
        "ConversionResult<T> convert(std::string_view)`, which every rule "
        "naming T then uses. Declaring only `using result = T` is not "
        "enough");
    return true;
  }
};

template <auto Member, str Literal, class Enum, int... I>
consteval bool enum_literal(std::integer_sequence<int, I...>) {
  constexpr auto values = [] {
    std::array<Enum, sizeof...(I)> out{};
    std::size_t used = 0;
    const auto add = [&](Enum value) {
      for (std::size_t at = 0; at < used; ++at)
        if (out[at] == value) return;
      out[used++] = value;
    };
    ((!enumerator_name<static_cast<Enum>(I + kEnumProbeMin)>().empty()
          ? add(static_cast<Enum>(I + kEnumProbeMin))
          : void()),
     ...);
    return std::pair{out, used};
  }();
  return [&]<std::size_t... J>(std::index_sequence<J...>) {
    return ::EnumLiteral<Member, Literal, Enum, values.first[J]...>();
  }(std::make_index_sequence<values.second>{});
}

template <auto Member, str Literal, class Value>
consteval bool LiteralConversion() {
  if constexpr (!default_convert<Value>(Literal.view()).value.has_value()) {
    if constexpr (std::is_same_v<Value, bool>)
      return ::BoolLiteral<Member, Literal>();
    else
      return enum_literal<Member, Literal, Value>(enum_probes{});
  }
  return true;
}

template <auto P, class Src>
consteval bool check_lit_write() {
  using X = typename field_tag<P>::held;
  // Type compatibility does not ensure a bool or enum literal names a value.
  if constexpr (field_tag<P>::nature == FieldNature::value &&
                (std::is_same_v<X, bool> || std::is_enum_v<X>))
    return LiteralConversion<P, Src::spelling, X>();
  else
    return true;
}

template <auto P, class... Srcs>
consteval bool check_lit_writes(TypeList<Srcs...>) {
  return (check_lit_write<P, Srcs>() && ...);
}

// Each source on its own.
template <auto P, class... Srcs>
consteval bool check_writes(TypeList<Srcs...>) {
  return (check_write<P, Srcs>::check() && ...);
}

// A rule or terminal paying text somewhere inside the shape: under a
// sequence, that payload never reaches the member.
template <class E, class Kids = typename ExprShape<E>::kids>
inline constexpr bool inner_text_pay = false;
template <class R>
inline constexpr bool inner_text_pay<Ref<R>> =
    !is_node_rule<named_rule_t<R>> && !is_infix_rule<named_rule_t<R>>;
template <class E, class... Ks>
  requires(grouping<E> || transparent<E>)
inline constexpr bool inner_text_pay<E, TypeList<Ks...>> =
    (inner_text_pay<Ks> || ...);
// A variant member with an alternative that is a value, not a node.
template <class H>
inline constexpr bool takes_values = false;
template <class... As>
inline constexpr bool takes_values<std::variant<As...>> =
    ((!std::is_same_v<As, std::monostate> && !alt_names_a_type<As>) || ...);

// Whether a shape builds a node, which a member that takes a value has no use
// for.
template <class E, class Kids = typename ExprShape<E>::kids>
inline constexpr bool builds_a_node = holds_directive<E>;
template <class E, class... Ks>
inline constexpr bool builds_a_node<E, TypeList<Ks...>> =
    holds_directive<E> || (builds_a_node<Ks> || ...);
template <class R>
inline constexpr bool builds_a_node<Ref<R>, TypeList<>> =
    is_node_rule<named_rule_t<R>> || is_infix_rule<named_rule_t<R>>;

// A shape may pay nothing -- a sequence holding a node, a repetition, a
// lookahead -- leaving no source for `check_write` to judge. The builder then
// reads that as "a node travels" and looks for one in the span, which only a
// family-naming member can receive, and only from a span holding one on every
// match and no repetition of them -- refused here rather than at the first
// document.
template <auto P, class E>
struct dir_payload_ok {
  using tag = field_tag<P>;
  static constexpr bool takes_a_node =
      alt_names_a_type<typename tag::held> && !is_ref_nature(tag::nature);
  static constexpr bool values_lost =
      takes_values<typename tag::held> && inner_text_pay<E>;
  using held = nodes_held<typename held_targets<typename tag::held>::type,
                          typename once_item<E>::type>;
  static constexpr bool counted = takes_a_node && !values_lost;
  static consteval bool check() {
    if constexpr (tl_size<typename payload_of<E>::type>::value == 0) {
      if constexpr (!takes_a_node && builds_a_node<E>)
        ::DirectiveNodeIntoValue<P>();
      if constexpr (!takes_a_node && !builds_a_node<E>)
        ::DirectiveWithoutValue<P>();
      if constexpr (values_lost) ::DirectiveLosesValues<P>();
      if constexpr (counted && held::repeated) ::DirectiveRepeatsNodes<P>();
      if constexpr (counted && !held::repeated && held::hi > 1)
        ::DirectiveHoldsTwoNodes<P>();
      if constexpr (counted && !held::repeated && held::lo == 0)
        ::DirectiveMayLackNode<P>();
    }
    return true;
  }
};
// The directive distributes over a choice, so each alternative pays on its
// own: the payloads of the others do not stand in for one that has none.
template <auto P, class... As>
struct dir_payload_ok<P, Choice<As...>> {
  static consteval bool check() {
    return (dir_payload_ok<P, As>::check() && ...);
  }
};
template <auto P, class E>
  requires transparent<E>
struct dir_payload_ok<P, E> : dir_payload_ok<P, through_t<E>> {};

// One directive per storage: the builder branches on the member's storage
// alone, so `assign` on a vector would silently accumulate and `append` on a
// scalar would silently overwrite. Not auto-repaired either -- that would
// change what an accumulating grammar means -- so the directive is named.
template <auto P, bool IsAppend>
consteval bool dir_storage_ok() {
  constexpr FieldStorage storage = field_tag<P>::storage;
  static_assert(!(IsAppend && storage != FieldStorage::vector),
                "`+=` appends, so it needs a `vector<T>` list member: on a "
                "single-value member each match would overwrite the last -- "
                "write `=`");
  static_assert(!(!IsAppend && storage == FieldStorage::vector),
                "`=` sets the single value of a member, and this member is "
                "a `vector<T>` list -- to add each match to the list, write "
                "`+=`");
  return true;
}

// A site may bind less than its member accepts. The narrow type must derive
// from exactly one of the member's candidates: one, because a filter only
// restricts and an outside type would accept what the member can't hold;
// exactly one, because the filter answers which candidate a tag satisfies.
template <auto P, class N>
consteval bool dir_narrow_ok() {
  if constexpr (!std::is_void_v<N>) {
    using tag = field_tag<P>;
    using targets = typename held_targets<typename tag::held>::type;
    constexpr bool on_a_reference = is_ref_nature(tag::nature);
    constexpr std::size_t bases =
        on_a_reference ? narrow_bases<N, targets>::count : 0;
    if constexpr (!on_a_reference) ::NarrowNonReference<P, N>();
    if constexpr (on_a_reference && bases == 0)
      ::NarrowOutsideCandidates<P, N>();
    if constexpr (on_a_reference && bases > 1)
      ::NarrowAmbiguousCandidate<P, N>();
  }
  return true;
}

// The walk: every directive in a rule body, payloads included (a region's
// body holds directives of its own).
template <class E, class Kids = typename ExprShape<E>::kids>
struct payloads_ok;
template <class E, class... Ks>
struct payloads_ok<E, TypeList<Ks...>>
    : std::bool_constant<(payloads_ok<Ks>::value && ...)> {};
template <auto S, class E, bool IsAppend>
inline constexpr bool spec_ok =
    dir_storage_ok<spec_ptr<S>, IsAppend>() &&
    dir_payload_ok<spec_ptr<S>, E>::check() &&
    dir_narrow_ok<spec_ptr<S>, spec_narrow_t<S>>() &&
    check_writes<spec_ptr<S>>(typename payload_of<E>::type{}) &&
    check_lit_writes<spec_ptr<S>>(typename lit_pays<E>::type{});

template <auto S, class E>
struct payloads_ok<Assign<S, E>, TypeList<E>>
    : std::bool_constant<spec_ok<S, E, false> && payloads_ok<E>::value> {};
// `+=` reaches the same held type, the container's element being it. Which
// storage it may name is `dir_storage_ok`'s question.
template <auto S, class E>
struct payloads_ok<Append<S, E>, TypeList<E>>
    : std::bool_constant<spec_ok<S, E, true> && payloads_ok<E>::value> {};

template <auto P>
consteval bool nest_ok() {
  static_assert(field_tag<P>::nature == FieldNature::node,
                "nest<T, &C::f> moves the node built so far into f, so the "
                "member must hold a node pointer");
  return true;
}
template <class T, auto P, class E>
struct payloads_ok<Nest<T, P, E>, TypeList<E>>
    : std::bool_constant<nest_ok<P>() && payloads_ok<E>::value> {};

// A climb whose primary builds no node has no node to put in its operands.
// Dispatched on a bool so the false side is not instantiated, `check_write`
// firing its asserts on instantiation.
template <bool NodePrimary, class R, class St>
consteval bool infix_operands_ok() {
  if constexpr (NodePrimary) {
    // Both halves: nested levels feed the climb's own node back into the
    // operands, and the primary's node enters them first.
    using self = pay_node<rule_type_t<R>>;
    using primary =
        pay_node<rule_type_t<typename St::template rule<typename R::primary>>>;
    return check_write<R::left_pointer, self>::check() &&
           check_write<R::right_pointer, self>::check() &&
           check_write<R::left_pointer, primary>::check() &&
           check_write<R::right_pointer, primary>::check();
  } else {
    static_assert(dependent_false<R>,
                  "cursiv: an infix AST requires a primary that builds a node");
    return false;
  }
}

// The operator member takes what every operator hands over: a bare literal's
// text, or the value a terminal's or a data rule's converter makes of it. A
// bare literal is its own whole payload, so its spelling must convert too.
// `Src` is the source the engine's lowering writes from, not the rule the
// author named: the check answers about the write that happens.
template <class R, class St, template <class, class, auto> class Src,
          std::size_t... Is>
consteval bool infix_ops_ok(std::index_sequence<Is...>) {
  constexpr auto P = R::op_pointer;
  const auto writes = []<class... Ops>(TypeList<Ops...>) {
    return (check_write<P, Src<St, Ops, P>>::check() && ...) &&
           (check_lit_writes<P>(typename lit_pays<Ops>::type{}) && ...);
  };
  return (writes(typename alts_list<
                 typename R::levels::template at<Is>::op_expr>::type{}) &&
          ...);
}

template <auto P, class R>
consteval bool infix_member_belongs() {
  using Owner =
      typename member_traits<std::remove_cv_t<decltype(P)>>::class_type;
  static_assert(
      std::is_base_of_v<Owner, typename R::node_type>,
      "cursiv: an infix member must belong to its node type or a base");
  return true;
}

// An infix rule synthesizes its body, so a mismatched member carries no
// directive to refuse it: the rule answers for its three members.
template <class R, class St, template <class, class, auto> class Src>
consteval bool infix_rule_ok() {
  using Primary = typename St::template rule<typename R::primary>;
  constexpr bool nodePrimary = is_node_rule<Primary> || is_infix_rule<Primary>;
  return infix_member_belongs<R::left_pointer, R>() &&
         infix_member_belongs<R::op_pointer, R>() &&
         infix_member_belongs<R::right_pointer, R>() &&
         infix_operands_ok<nodePrimary, R, St>() &&
         infix_ops_ok<R, St, Src>(std::make_index_sequence<R::levels::size>{});
}
// Asked one rule at a time, so what the check instantiates is named by the
// stage and that rule, never by the whole list of rules reached.
template <class St, template <class, class, auto> class Src, class R>
consteval bool infix_rule_reached_ok() {
  using T = typename St::template rule<R>;
  if constexpr (is_infix_rule<T>)
    return infix_rule_ok<T, St, Src>();
  else
    return true;
}
template <class St, template <class, class, auto> class Src, class... Rs>
consteval bool infix_rules_ok(TypeList<Rs...>) {
  return (infix_rule_reached_ok<St, Src, Rs>() && ...);
}

}  // namespace cursiv::detail
