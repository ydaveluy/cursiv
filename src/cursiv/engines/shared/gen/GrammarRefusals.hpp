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
                "path: `=` overwrites, so the first value would be lost "
                "and a node would keep the "
                "parent it was given. Write it once, or `+=` into a "
                "`vector<T>` list member");
  return false;
}();

template <class Rule, auto Member>
inline constexpr bool WrittenEachTurn = [] {
  static_assert(cursiv::detail::dependent_false<Rule>,
                "this repetition writes the member on every turn and `=` "
                "overwrites: only the last turn's value would stay. "
                "Append into a `vector<T>` list "
                "member, or write it once outside the repetition");
  return false;
}();

template <class Rule, auto Member>
inline constexpr bool WrittenOverAdopted = [] {
  static_assert(cursiv::detail::dependent_false<Rule>,
                "this directive writes a member the node of the rule called "
                "before it may have written already: a call adopts its node, "
                "and `=` would overwrite what the rule put there. Write the "
                "member in one place: in the called rule, or in a fragment "
                "spelled where the call stood");
  return false;
}();

template <class Node>
consteval bool NodeClass() {
  static_assert(
      std::is_base_of_v<cursiv::AstNode, Node>,
      "this rule builds a class which does not derive from cursiv::AstNode "
      "-- the builder wires `parent` and the type tag through that base");
  return true;
}

template <class Node>
consteval bool BuildableNode() {
  constexpr bool derived = std::is_base_of_v<cursiv::AstNode, Node>;
  static_assert(::NodeClass<Node>(),
                "a rule builds a class that is no AST node: the message "
                "above says which");
  static_assert(!(derived && std::is_abstract_v<Node>),
                "this rule builds an ABSTRACT class -- name the concrete "
                "class the rule produces, or give the branches a common "
                "concrete base");
  static_assert(!(derived && !std::is_abstract_v<Node> &&
                  !std::is_default_constructible_v<Node>),
                "this rule builds a class which has no default constructor "
                "-- the builder creates the node empty and the directives "
                "fill it, so every member needs a default");
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
                "this member holds a node which does not derive from "
                "cursiv::AstNode -- the builder wires `parent` and the type "
                "tag through that base");
  return false;
}

template <auto Member, class Value>
consteval bool BorrowedTextTemporary() {
  static_assert(
      cursiv::detail::dependent_false<Value>,
      "a string_view member BORROWS its text, so the payload's return type "
      "must BE string_view -- this converter answers a value which the "
      "builder MATERIALISES as a temporary and destroys the instant the "
      "member is written: the view would point at freed bytes. THE TEXT "
      "LAW, and the converter's return type decides it: answer a string_view "
      "into the input, or -- when the value has to be built -- take the "
      "resource the builder offers and answer a view of what you allocated "
      "there (`static std::string_view convert(std::string_view, "
      "std::pmr::memory_resource*)`), which lives as long as the ParseResult");
  return false;
}

template <auto Member, cursiv::str Literal>
consteval bool BoolLiteral() {
  static_assert(
      cursiv::detail::dependent_false<decltype(Member)>,
      "this literal can never convert: it is the whole payload written into "
      "a bool member, and no bool reads that text -- every match would be a "
      "conversion diagnostic. A bool set by the FACT of a match is `?=`: "
      "write `flag<&C::f>(...)`, not `assign`; a bool read by value "
      "takes `true` or `false`");
  return false;
}

template <auto Member, cursiv::str Literal, class Enum, Enum... Accepted>
consteval bool EnumLiteral() {
  static_assert(
      cursiv::detail::dependent_false<Enum>,
      "this literal can never convert: it is the whole payload written into "
      "the enum member, and no enumerator reads that text -- every match "
      "would be a conversion diagnostic. An enum member takes an "
      "ENUMERATOR NAME, spelled exactly: match one of the Accepted "
      "enumerators, or give the payload a converter answering the enum. "
      "An enumerator is read by name only within "
      "`CURSIV_ENUM_PROBE_MIN`..`CURSIV_ENUM_PROBE_MAX` (-64..512 unless "
      "defined)");
  return false;
}

template <auto Member>
consteval bool DirectiveWithoutValue() {
  static_assert(
      cursiv::detail::dependent_false<decltype(Member)>,
      "this directive stands over a shape that produces no value -- a "
      "quantifier alone (option, many, some, repeat) over no node, or a "
      "condition alone -- and the member takes one. Put the directive "
      "inside the quantifier (`option(assign<&T::f>(ref<Part>))`), or give "
      "the span a rule of its own and write that rule");
  return false;
}

template <auto Member>
consteval bool DirectiveNodeIntoValue() {
  static_assert(
      cursiv::detail::dependent_false<decltype(Member)>,
      "this directive stands over a shape that builds a node, and the member "
      "takes a value, not a node. Give the member a node type, or write the "
      "part that carries the value (`assign<&T::f>(ref<Part>)` inside the "
      "shape)");
  return false;
}

template <auto Member>
consteval bool DirectiveLosesValues() {
  static_assert(
      cursiv::detail::dependent_false<decltype(Member)>,
      "this directive stands over a sequence, and only a node travels "
      "through one -- the member also takes values, which a rule or "
      "terminal inside would pay and the member never receive. Name the "
      "ONE part the member reads (`assign<&T::f>(ref<Part>)` inside the "
      "sequence)");
  return false;
}

template <auto Member>
consteval bool DirectiveRepeatsNodes() {
  static_assert(
      cursiv::detail::dependent_false<decltype(Member)>,
      "this directive stands over a repetition that can hold SEVERAL "
      "nodes, and the member takes ONE per write: the first would be kept "
      "and the rest dropped. Put the directive under the repetition "
      "(`many(append<&T::f>(ref<Item>), sep)`), or on the one part the "
      "member reads");
  return false;
}

template <auto Member>
consteval bool DirectiveHoldsTwoNodes() {
  static_assert(
      cursiv::detail::dependent_false<decltype(Member)>,
      "this directive stands over a sequence that can hold TWO nodes "
      "for the member, and the member takes ONE: the first would be kept "
      "and the other dropped. Create the node that holds both "
      "(`create<T>(assign<&T::a>(ref<A>) + assign<&T::b>(ref<B>))`) and "
      "write that, or put the directive on the one part the member reads");
  return false;
}

template <auto Member>
consteval bool DirectiveMayLackNode() {
  static_assert(
      cursiv::detail::dependent_false<decltype(Member)>,
      "this directive stands over a shape that can match with NO node "
      "for the member -- a sequence of literals, a part that may be "
      "absent, a node of another family, or one a directive inside already "
      "takes -- and the member takes one: such a match would leave nothing "
      "to write. Give every alternative the node the member reads, or make "
      "the directive's own part optional (`option(assign<&T::f>(ref<Part>))`)");
  return false;
}

template <auto Member, class Narrow>
consteval bool NarrowNonReference() {
  static_assert(
      cursiv::detail::dependent_false<Narrow>,
      "this member is not a cross-reference, so this site has no candidates "
      "to narrow: a narrow type restricts WHICH of a reference's classes "
      "one site may bind, and every other member already names the one "
      "class it holds");
  return false;
}

template <auto Member, class Narrow>
consteval bool NarrowOutsideCandidates() {
  static_assert(
      cursiv::detail::dependent_false<Narrow>,
      "this site narrows the member to a class which derives from none "
      "of the classes the member can hold -- a narrowing RESTRICTS what "
      "the member already accepts, so name one of its candidates or a "
      "class deriving from one");
  return false;
}

template <auto Member, class Narrow>
consteval bool NarrowAmbiguousCandidate() {
  static_assert(
      cursiv::detail::dependent_false<Narrow>,
      "this site narrows the member to a class which derives from SEVERAL "
      "of the member's candidates -- the filter answers WHICH candidate "
      "a target satisfies, and there would be two right answers, so "
      "`get<T>()` would depend on the order the candidates happen to be "
      "written in. Narrow to a class deriving from exactly one of them");
  return false;
}
