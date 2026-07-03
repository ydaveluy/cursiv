#pragma once

/// Base grammar element contracts and the shared element kind enumeration.
#include <ostream>

namespace pegium::grammar {

enum class ElementKind {

  Create,     // create a new instance
  Nest,       // create a new instance and init a feature with current value
  Assignment, // assign a feature of the current element
  AndPredicate,
  AnyCharacter,
  CharacterRange,
  DataTypeRule,
  Group,
  Literal,
  NotPredicate,
  OrderedChoice,
  ParserRule,
  Repetition,
  TerminalRule,
  UnorderedGroup,
  InfixRule,
  InfixOperator
};

struct AbstractElement {
  using ElementKind = pegium::grammar::ElementKind;

  constexpr explicit AbstractElement(ElementKind kind) noexcept : _kind(kind) {}

  /// Returns the element kind. Stored once at construction (grammar bootstrap)
  /// rather than dispatched virtually, so hot readers reaching it through a
  /// `const AbstractElement *` (e.g. `getGrammarElement()->getKind()`) pay a
  /// plain field load instead of a vtable indirection that cannot be
  /// devirtualised through the base pointer.
  [[nodiscard]] constexpr ElementKind getKind() const noexcept { return _kind; }

  constexpr virtual bool isNullable() const noexcept = 0;
  constexpr virtual ~AbstractElement() noexcept = default;
  constexpr virtual void print(std::ostream &os) const = 0;

  friend std::ostream &operator<<(std::ostream &os,
                                  const AbstractElement &obj) {
    obj.print(os);
    return os;
  }

private:
  ElementKind _kind;
};

} // namespace pegium::grammar
