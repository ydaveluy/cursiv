// These generator diagnostics are global so compiler backtraces start with
// the mistake and its grammar types, without an implementation namespace.
#pragma once

#include <type_traits>

#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/utils/ctstring.hpp"
#include "cursiv/core/utils/typelist.hpp"

template <class Rule, auto Member>
inline constexpr bool WrittenAgain = [] {
  static_assert(cursiv::detail::dependent_false<Rule>,
                "this directive writes the member a second time on one "
                "path: `=` overwrites, so the first value would be lost, and "
                "a node written there would keep the parent it was given -- "
                "write the member once, or append with `+=` into a "
                "`vector<T>` list member");
  return false;
}();

template <class Rule, auto Member>
inline constexpr bool WrittenEachTurn = [] {
  static_assert(cursiv::detail::dependent_false<Rule>,
                "this repetition writes the member on every turn and `=` "
                "overwrites, so only the value of the last turn would stay "
                "-- append into a `vector<T>` list member, or write the "
                "member once outside the repetition");
  return false;
}();

template <class Rule, auto Member>
inline constexpr bool WrittenOverAdopted = [] {
  static_assert(cursiv::detail::dependent_false<Rule>,
                "this directive writes a member that the rule called before "
                "it may already have written: the call hands over its node, "
                "so `=` would overwrite what that rule put there -- write the "
                "member in one place: in the called rule, or in a fragment "
                "used in place of the call");
  return false;
}();

template <class Node>
consteval bool NodeClass() {
  static_assert(
      std::is_base_of_v<cursiv::AstNode, Node>,
      "this rule builds a class that does not derive from cursiv::AstNode "
      "-- derive it from cursiv::AstNode, which holds the `parent` link and "
      "the type tag");
  return true;
}

template <class Node>
consteval bool BuildableNode() {
  constexpr bool derived = std::is_base_of_v<cursiv::AstNode, Node>;
  static_assert(::NodeClass<Node>(),
                "a rule builds a class that is no AST node: the message "
                "above says which");
  static_assert(!(derived && std::is_abstract_v<Node>),
                "this rule builds an abstract class -- name the concrete "
                "class the rule builds, or give the branches a common "
                "concrete base");
  static_assert(!(derived && !std::is_abstract_v<Node> &&
                  !std::is_default_constructible_v<Node>),
                "this rule builds a class that has no default constructor "
                "-- the builder creates the node empty and the directives "
                "fill it, so give every member a default value");
  return true;
}

template <class Node, class Base>
consteval bool AstBase() {
  static_assert(
      std::is_same_v<Node, Base> || !std::is_base_of_v<Base, Node> ||
          cursiv::ast_type_tag<Node>.derivesFrom(&cursiv::ast_type_tag<Base>),
      "this class derives from an AST base without `Extends`: a reference "
      "to the base would never resolve to it. Declare it `Extends<Base>`");
  return true;
}

template <auto Member, class Node>
consteval bool MemberNodeOutsideAst() {
  static_assert(cursiv::detail::dependent_false<Node>,
                "this member holds a node that does not derive from "
                "cursiv::AstNode -- derive its class from cursiv::AstNode, "
                "which holds the `parent` link and the type tag");
  return false;
}

template <auto Member, class Value>
consteval bool BorrowedTextTemporary() {
  static_assert(
      cursiv::detail::dependent_false<Value>,
      "a string_view member points into text, so its converter must return "
      "a string_view: this converter returns another value, which the "
      "builder keeps only as a temporary, so the view would point at freed "
      "memory -- return a string_view into the input, or, when the value "
      "must be built, allocate it in the resource the builder passes and "
      "return a view of it (`static std::string_view convert(std::string_view, "
      "std::pmr::memory_resource*)`), which lives as long as the ParseResult");
  return false;
}

template <auto Member, cursiv::str Literal>
consteval bool BoolLiteral() {
  static_assert(
      cursiv::detail::dependent_false<decltype(Member)>,
      "this literal can never convert: it is the whole value written into a "
      "bool member, and only `true` and `false` convert to a bool, so every "
      "match would report a conversion error -- to set a bool when the text "
      "matches, write `flag<&C::f>(...)` instead of `assign`");
  return false;
}

template <auto Member, cursiv::str Literal, class Enum, Enum... Accepted>
consteval bool EnumLiteral() {
  static_assert(
      cursiv::detail::dependent_false<Enum>,
      "this literal can never convert: it is the whole value written into "
      "the enum member, and it is not the name of an enumerator, so every "
      "match would report a conversion error -- write the exact name of one "
      "of the Accepted enumerators, or give the value a converter that "
      "returns the enum. Enumerators are found by name only between "
      "`CURSIV_ENUM_PROBE_MIN` and `CURSIV_ENUM_PROBE_MAX` (-64 and 512 "
      "unless defined)");
  return false;
}

template <auto Member>
consteval bool DirectiveWithoutValue() {
  static_assert(
      cursiv::detail::dependent_false<decltype(Member)>,
      "this directive applies to an expression that gives no value -- a "
      "quantifier (option, many, some, repeat) with no node in it, or a "
      "condition alone -- and the member needs one: put the directive "
      "inside the quantifier (`option(assign<&T::f>(ref<Part>))`), or give "
      "the expression a rule of its own and write that rule");
  return false;
}

template <auto Member>
consteval bool DirectiveNodeIntoValue() {
  static_assert(
      cursiv::detail::dependent_false<decltype(Member)>,
      "this directive applies to an expression that builds a node, but the "
      "member takes a value, not a node -- give the member a node type, or "
      "put the directive on the part that carries the value "
      "(`assign<&T::f>(ref<Part>)` inside the expression)");
  return false;
}

template <auto Member>
consteval bool DirectiveLosesValues() {
  static_assert(
      cursiv::detail::dependent_false<decltype(Member)>,
      "this directive applies to a sequence, and only a node passes through "
      "a sequence: the member also takes values, which a rule or a terminal "
      "inside would give and the member never receive -- put the directive "
      "on the one part the member takes (`assign<&T::f>(ref<Part>)` inside "
      "the sequence)");
  return false;
}

template <auto Member>
consteval bool DirectiveRepeatsNodes() {
  static_assert(
      cursiv::detail::dependent_false<decltype(Member)>,
      "this directive applies to a repetition that can hold several nodes, "
      "but the member takes one per write, so only the first would be kept "
      "-- put the directive inside the repetition "
      "(`many(append<&T::f>(ref<Item>), sep)`), or on the one part the "
      "member takes");
  return false;
}

template <auto Member>
consteval bool DirectiveHoldsTwoNodes() {
  static_assert(
      cursiv::detail::dependent_false<decltype(Member)>,
      "this directive applies to a sequence that can hold two nodes for the "
      "member, but the member takes one, so only the first would be kept -- "
      "create a node that holds both "
      "(`create<T>(assign<&T::a>(ref<A>) + assign<&T::b>(ref<B>))`) and "
      "write that, or put the directive on the one part the member takes");
  return false;
}

template <auto Member>
consteval bool DirectiveMayLackNode() {
  static_assert(
      cursiv::detail::dependent_false<decltype(Member)>,
      "this directive applies to an expression that can match without a "
      "node for the member, such as a sequence of literals, a part that may "
      "be absent, a node of an unrelated class, or one that a directive "
      "inside already takes: such a match would leave nothing to write -- "
      "give every alternative a node for the member, or make the directive "
      "optional (`option(assign<&T::f>(ref<Part>))`)");
  return false;
}

template <auto Member, class Narrow>
consteval bool NarrowNonReference() {
  static_assert(
      cursiv::detail::dependent_false<Narrow>,
      "this member is not a cross-reference, so there is nothing to narrow: "
      "a narrowing limits which classes a reference may point to at one "
      "place, and any other member already names the one class it holds");
  return false;
}

template <auto Member, class Narrow>
consteval bool NarrowOutsideCandidates() {
  static_assert(
      cursiv::detail::dependent_false<Narrow>,
      "this narrowing names a class that derives from none of the classes "
      "the member can hold, and a narrowing only limits what the member "
      "already accepts -- name one of those classes, or a class derived "
      "from one");
  return false;
}

template <auto Member, class Narrow>
consteval bool NarrowAmbiguousCandidate() {
  static_assert(
      cursiv::detail::dependent_false<Narrow>,
      "this narrowing names a class that derives from several of the "
      "classes the member can hold, so `get<T>()` would depend on the order "
      "in which they are written -- narrow to a class derived from exactly "
      "one of them");
  return false;
}
