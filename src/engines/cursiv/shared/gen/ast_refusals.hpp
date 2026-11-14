#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "cursiv/grammar/Conversion.hpp"
#include "cursiv/grammar/ExprShape.hpp"
#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/grammar/GrammarUtils.hpp"
#include "cursiv/grammar/PayloadOf.hpp"
#include "cursiv/grammar/expr.hpp"
#include "cursiv/shared/gen/GrammarRefusals.hpp"
#include "cursiv/shared/runtime/ast_fields.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"
#include "cursiv/syntax-tree/AstField.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/MemberTraits.hpp"
#include "cursiv/syntax-tree/VariantNodeAlt.hpp"
#include "cursiv/utils/TypeName.hpp"
#include "cursiv/utils/ctstring.hpp"
#include "cursiv/utils/typelist.hpp"

// Why a grammar is refused for the shape of its AST: one static_assert per
// mistake a member and its payload can make, each guarded by the case it
// diagnoses, so exactly one fires and the message names the actual mistake.

namespace cursiv::engine {

// How many nodes of a member's family one match of a shape leaves for the
// directive above it: what that directive's node path takes when the shape
// gives no value of its own.

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

template <auto P, class Src>
struct check_write {
  using tag = field_tag<P>;
  using X =
      cond_t<writes_whole<tag, Src>, typename tag::member, typename tag::held>;
  static constexpr bool ok = admits<X, Src>();
  static constexpr FieldContent content = tag::content;
  // A pointer to any class is a node member, so "the pointee is off the
  // AstNode family" and "the payload is the wrong family" are two mistakes.
  static constexpr bool pointee_ok =
      content != FieldContent::node ||
      std::is_base_of_v<AstNode, std::remove_pointer_t<X>>;
  // A `string_view` member refusing its text: the value is not what the source
  // spells, and the generic "no conversion" message below would mislead.
  static constexpr bool view_dangles =
      content == FieldContent::value &&
      std::is_same_v<X, AstNode::string_view> && is_text_payload<Src> && !ok;
  static constexpr bool cuts_number =
      narrows<X, typename payload_value<Src>::type>;
  // A value that replaces what the member holds needs an assignment; a list's
  // or an optional's element and a variant's alternative are built in place.
  static constexpr bool replaceable =
      !(std::is_same_v<typename payload_value<Src>::type,
                       typename tag::member> ||
        (tag::storage == FieldStorage::direct &&
         content == FieldContent::value)) ||
      std::is_assignable_v<typename tag::member&,
                           typename payload_value<Src>::type> ||
      std::is_move_assignable_v<typename tag::member>;
  static constexpr bool list_grows =
      tag::storage != FieldStorage::vector ||
      std::is_move_constructible_v<typename tag::held>;
  // Mixing in a reference alternative shifts the interpreter's positions
  // apart from the writer's, surfacing as `malformed_cst` on every document.
  static constexpr bool variant_alts_index_targets =
      content != FieldContent::variant || alts_index_targets<X>::value;

  static consteval bool check() {
    static_assert(!(is_ref_content(content) && !is_text_payload<Src>),
                  "a reference member takes text: a cross-reference stores "
                  "the name as written, from a terminal or a data rule, never "
                  "a node built by a rule");
    static_assert(!(is_ref_content(content) && is_text_payload<Src> && !ok),
                  "a reference member holds a name, so its value must be text: "
                  "give the rule a converter that returns a string or a "
                  "string_view");
    if constexpr (!pointee_ok)
      ::MemberNodeOutsideAst<P, std::remove_pointer_t<X>>();
    static_assert(!(content == FieldContent::node && pointee_ok && !ok),
                  "this member holds a node of its own type: the value "
                  "written must be a node whose type derives from the type "
                  "the member points to (from a rule that builds a node, a "
                  "region, or a nest)");
    static_assert(!(content == FieldContent::variant && !ok),
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
      ::ViewOfOtherText<P, typename payload_value<Src>::type>();
    // Fires for a node payload or an unbuildable converter result: an enum
    // converts text by its enumerator names.
    static_assert(!cuts_number,
                  "this rule gives a number this member cannot always hold: "
                  "make the member wider, or give the rule the member's own "
                  "type");
    static_assert(replaceable,
                  "this member's type cannot be assigned, so nothing can "
                  "replace the value it starts with: hold it in `optional<T>` "
                  "or `vector<T>`, which build it in place, or give the type "
                  "an assignment");
    static_assert(list_grows,
                  "a list moves its elements as it grows, and this type cannot "
                  "be moved: give it a move constructor, or use `optional<T>`");
    static_assert(
        !(content == FieldContent::value && !ok && !view_dangles &&
          !cuts_number),
        "no conversion from this value to the member type: the built-in "
        "conversions cover string_view, bool, arithmetic types and enums, and "
        "nothing else -- give the rule a converter that returns the member's "
        "type: `as<f>(...)` around the rule's body, or a "
        "`cursiv::default_converter<T>` specialisation declaring `static "
        "ConversionResult<T> convert(std::string_view)`, which then converts "
        "every member of type T");
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
    ((!probed_name<Enum, I>().empty()
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

// Whether a literal may read as a number of type `X`. The conversion is no
// constant expression, so this asks only what no number can be without: a sign
// at most, then digits, or for a floating type what may open one.
template <class X>
consteval bool may_spell_number(std::string_view text) {
  if (text.starts_with('+')) text.remove_prefix(1);
  if (text.starts_with('-')) text.remove_prefix(1);
  const auto digit = [](char c) { return c >= '0' && c <= '9'; };
  if constexpr (std::is_floating_point_v<X>)
    return !text.empty() &&
           (digit(text[0]) ||
            std::string_view(".iInN").find(text[0]) != std::string_view::npos);
  else
    return !text.empty() && std::ranges::all_of(text, digit);
}

template <auto P, class Src>
consteval bool check_lit_write() {
  using X = typename field_tag<P>::held;
  if constexpr (field_tag<P>::content != FieldContent::value) {
    return true;
  } else if constexpr (std::is_same_v<X, bool> || std::is_enum_v<X>) {
    // Type compatibility does not ensure the literal names a value.
    return LiteralConversion<P, Src::spelling, X>();
  } else if constexpr (from_chars_number<X> &&
                       !may_spell_number<X>(Src::spelling.view())) {
    return ::NumberLiteral<P, Src::spelling>();
  } else {
    return true;
  }
}

template <auto P, class... Srcs>
consteval bool check_lit_writes(TypeList<Srcs...>) {
  return (check_lit_write<P, Srcs>() && ...);
}

template <auto P, class... Srcs>
consteval bool check_writes(TypeList<Srcs...>) {
  return (check_write<P, Srcs>::check() && ...);
}

// A rule or terminal giving text somewhere inside the shape: under a
// sequence, that payload never reaches the member.
template <class E, class Children = typename ExprShape<E>::children>
inline constexpr bool inner_text_payload = false;
template <class R>
inline constexpr bool inner_text_payload<Ref<R>> =
    !is_node_rule<named_rule_t<R>> && !is_infix_rule<named_rule_t<R>>;
template <class E, class... Ks>
  requires(grouping<E> || transparent<E>)
inline constexpr bool inner_text_payload<E, TypeList<Ks...>> =
    (inner_text_payload<Ks> || ...);
// A variant member with an alternative that is a value, not a node.
template <class H>
inline constexpr bool takes_values = false;
template <class... As>
inline constexpr bool takes_values<std::variant<As...>> =
    ((!std::is_same_v<As, std::monostate> && !alt_names_a_type<As>) || ...);

// Whether a shape builds a node, which a member that takes a value has no use
// for.
template <class E, class Children = typename ExprShape<E>::children>
inline constexpr bool builds_a_node = holds_directive<E>;
template <class E, class... Ks>
inline constexpr bool builds_a_node<E, TypeList<Ks...>> =
    holds_directive<E> || (builds_a_node<Ks> || ...);
template <class R>
inline constexpr bool builds_a_node<Ref<R>, TypeList<>> =
    is_node_rule<named_rule_t<R>> || is_infix_rule<named_rule_t<R>>;

// A shape that gives no value gives its member the node in its span: only a
// member naming a family receives it, from a span holding exactly one node.
template <auto P, class E>
struct dir_payload_ok {
  using tag = field_tag<P>;
  static constexpr bool takes_a_node =
      alt_names_a_type<typename tag::held> && !is_ref_content(tag::content);
  static constexpr bool values_lost =
      takes_values<typename tag::held> && inner_text_payload<E>;
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
// The directive distributes over a choice, so each alternative is checked on
// its own: the payloads of the others do not stand in for one that has none.
template <auto P, class... As>
struct dir_payload_ok<P, Choice<As...>> {
  static consteval bool check() {
    return (dir_payload_ok<P, As>::check() && ...);
  }
};
template <auto P, class E>
  requires transparent<E>
struct dir_payload_ok<P, E> : dir_payload_ok<P, through_t<E>> {};

// One directive per storage: `append` to a vector, `assign` to anything else.
// `=` on a list takes the whole list a rule answers.
template <auto P, bool IsAppend, class... Srcs>
consteval bool dir_storage_ok(TypeList<Srcs...>) {
  using F = field_tag<P>;
  constexpr FieldStorage storage = F::storage;
  constexpr bool anyWhole = (writes_whole<F, Srcs> || ...);
  constexpr bool allWhole =
      sizeof...(Srcs) != 0 && (writes_whole<F, Srcs> && ...);
  static_assert(!(IsAppend && storage != FieldStorage::vector),
                "`+=` appends, so it needs a `vector<T>` list member: on a "
                "single-value member each match would overwrite the last -- "
                "write `=`");
  static_assert(!(IsAppend && anyWhole),
                "`+=` adds one element, and this rule returns the whole list "
                "-- write `=`");
  static_assert(!(!IsAppend && storage == FieldStorage::vector && !allWhole),
                "`=` sets the single value of a member, and this member is "
                "a `vector<T>` list -- to add each match to the list, write "
                "`+=`; `=` takes only a rule that returns that whole list");
  return true;
}

// A site may bind less than its member accepts: the narrow type must derive
// from exactly one of the member's candidates.
template <auto P, class N>
consteval bool dir_narrow_ok() {
  if constexpr (!std::is_void_v<N>) {
    using tag = field_tag<P>;
    using targets = typename held_targets<typename tag::held>::type;
    constexpr bool on_a_reference = is_ref_content(tag::content);
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
template <class E, class Children = typename ExprShape<E>::children>
struct payloads_ok;
template <class E, class... Ks>
struct payloads_ok<E, TypeList<Ks...>>
    : std::bool_constant<(payloads_ok<Ks>::value && ...)> {};
template <auto S, class E, bool IsAppend>
inline constexpr bool spec_ok =
    dir_storage_ok<spec_ptr<S>, IsAppend>(typename payload_of<E>::type{}) &&
    dir_payload_ok<spec_ptr<S>, E>::check() &&
    dir_narrow_ok<spec_ptr<S>, spec_narrow_t<S>>() &&
    check_writes<spec_ptr<S>>(typename payload_of<E>::type{}) &&
    check_lit_writes<spec_ptr<S>>(typename lit_payloads<E>::type{});

template <auto S, class E>
struct payloads_ok<Assign<S, E>, TypeList<E>>
    : std::bool_constant<spec_ok<S, E, false> && payloads_ok<E>::value> {};
// `+=` reaches the same held type, the container's element being it. Which
// storage it may name is `dir_storage_ok`'s question.
template <auto S, class E>
struct payloads_ok<Append<S, E>, TypeList<E>>
    : std::bool_constant<spec_ok<S, E, true> && payloads_ok<E>::value> {};

// A creation holding a second one: the writer over it takes one node and the
// other is lost. In a rule's own alternative the rule's check says so; here a
// directive stands over it.
template <class E>
consteval bool holds_one_node() {
  static_assert(!detail::mat::look<E>::may,
                "a create<> or nest<> here starts a second node inside it, and "
                "the directive over it writes only one of the two: the other "
                "is lost -- store the inner node in a member of the outer one "
                "with `assign` or `append`, or drop one of the two");
  return true;
}
template <class T, class E>
struct payloads_ok<Create<T, E>, TypeList<E>>
    : std::bool_constant<holds_one_node<E>() && payloads_ok<E>::value> {};

template <auto P>
consteval bool nest_ok() {
  static_assert(field_tag<P>::content == FieldContent::node,
                "nest<T, &C::f> moves the node built so far into f, so the "
                "member must hold a node pointer");
  return true;
}
template <class T, auto P, class E>
struct payloads_ok<Nest<T, P, E>, TypeList<E>>
    : std::bool_constant<nest_ok<P>() && holds_one_node<E>() &&
                         payloads_ok<E>::value> {};

// A climb whose primary builds no node has no node to put in its operands.
// Dispatched on a bool so the false side is not instantiated, `check_write`
// firing its asserts on instantiation.
template <bool NodePrimary, class R, class St>
consteval bool infix_operands_ok() {
  if constexpr (NodePrimary) {
    // Both halves: nested levels feed the climb's own node back into the
    // operands, and the primary's node enters them first.
    using self = node_payload<rule_type_t<R>>;
    using primary = node_payload<
        rule_type_t<typename St::template rule<typename R::primary>>>;
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
// text, which must convert too, or a converter's value. `Src` is the source
// the lowering writes from.
template <class R, class St, template <class, class, auto> class Src,
          std::size_t... Is>
consteval bool infix_ops_ok(std::index_sequence<Is...>) {
  constexpr auto P = R::op_pointer;
  const auto writes = []<class... Ops>(TypeList<Ops...>) {
    return (check_write<P, Src<St, Ops, P>>::check() && ...) &&
           (check_lit_writes<P>(typename lit_payloads<Ops>::type{}) && ...);
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
// directive to refuse it: the rule checks its three members.
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

}  // namespace cursiv::engine
