#pragma once

/// Pure grammar-shape predicates used by `Repetition` recovery to decide, once
/// per instance, whether an iteration body can be repaired by inserting a
/// terminal / is anchored by a required literal / begins with a synthetic
/// terminal followed by an unassigned lexical tail. These are free functions
/// over the grammar tree with no dependency on the parse/recovery state machine
/// (no `ParseContext`/`RecoveryContext`), cached behind `Repetition::shapeFacts()`.

#include <cstddef>
#include <cstdint>

#include <pegium/core/grammar/AbstractElement.hpp>
#include <pegium/core/grammar/Assignment.hpp>
#include <pegium/core/grammar/DataTypeRule.hpp>
#include <pegium/core/grammar/Group.hpp>
#include <pegium/core/grammar/Literal.hpp>
#include <pegium/core/grammar/OrderedChoice.hpp>
#include <pegium/core/grammar/ParserRule.hpp>
#include <pegium/core/grammar/Repetition.hpp>
#include <pegium/core/grammar/TerminalRule.hpp>
#include <pegium/core/parser/TerminalShape.hpp>

namespace pegium::parser::detail {

using grammar::ElementKind;

/// Walks the grammar subtree starting at `element` and returns whether
/// the *first non-nullable, non-wrapping leaf* satisfies `LeafPred`.
/// Recursion shape:
///
///   - `Assignment` / `TerminalRule` / `DataTypeRule` / `ParserRule`
///     unwrap to their inner element;
///   - `Group`: skip nullable children, recurse into the first
///     non-nullable;
///   - `OrderedChoice`: every branch must satisfy `LeafPred`;
///   - `Repetition`: only consider it if `min > 0`, then recurse
///     into its element;
///   - everything else delegates to `LeafPred(element)`.
///
/// Depth cap of 24 — grammars deeper than that are pathological and
/// the walker returns `false`.
template <typename LeafPred>
[[nodiscard]] bool walk_grammar_first_non_nullable_satisfies(
    const grammar::AbstractElement &element, LeafPred leafPred,
    std::uint8_t depth = 0) noexcept {
  if (depth > 24U) {
    return false;
  }
  const auto next = static_cast<std::uint8_t>(depth + 1U);
  switch (element.getKind()) {
  case ElementKind::Assignment:
    return walk_grammar_first_non_nullable_satisfies(
        *static_cast<const grammar::Assignment &>(element).getElement(),
        leafPred, next);
  case ElementKind::TerminalRule:
    return walk_grammar_first_non_nullable_satisfies(
        *static_cast<const grammar::TerminalRule &>(element).getElement(),
        leafPred, next);
  case ElementKind::DataTypeRule:
    return walk_grammar_first_non_nullable_satisfies(
        *static_cast<const grammar::DataTypeRule &>(element).getElement(),
        leafPred, next);
  case ElementKind::ParserRule:
    return walk_grammar_first_non_nullable_satisfies(
        *static_cast<const grammar::ParserRule &>(element).getElement(),
        leafPred, next);
  case ElementKind::Group: {
    const auto &group = static_cast<const grammar::Group &>(element);
    for (std::size_t i = 0; i < group.size(); ++i) {
      const auto &child = *group.get(i);
      if (child.isNullable()) {
        continue;
      }
      return walk_grammar_first_non_nullable_satisfies(child, leafPred,
                                                        next);
    }
    return false;
  }
  case ElementKind::OrderedChoice: {
    const auto &choice =
        static_cast<const grammar::OrderedChoice &>(element);
    if (choice.size() == 0U) {
      return false;
    }
    for (std::size_t i = 0; i < choice.size(); ++i) {
      if (!walk_grammar_first_non_nullable_satisfies(*choice.get(i),
                                                      leafPred, next)) {
        return false;
      }
    }
    return true;
  }
  case ElementKind::Repetition: {
    const auto &repetition =
        static_cast<const grammar::Repetition &>(element);
    return repetition.getMin() > 0U &&
           walk_grammar_first_non_nullable_satisfies(
               *repetition.getElement(), leafPred, next);
  }
  default:
    return leafPred(element);
  }
}

[[nodiscard]] inline bool starts_with_insertable_terminal(
    const grammar::AbstractElement &element) noexcept {
  return walk_grammar_first_non_nullable_satisfies(
      element, [](const grammar::AbstractElement &leaf) noexcept {
        if (leaf.getKind() != ElementKind::Literal) {
          return false;
        }
        const auto value =
            static_cast<const grammar::Literal &>(leaf).getValue();
        return make_terminal_shape_from_literal(value).allowsInsert();
      });
}

[[nodiscard]] inline bool
is_unassigned_lexical_tail(const grammar::AbstractElement &element,
                           std::uint8_t depth = 0) noexcept {
  return walk_grammar_first_non_nullable_satisfies(
      element,
      [](const grammar::AbstractElement &leaf) noexcept {
        using enum ElementKind;
        switch (leaf.getKind()) {
        case TerminalRule:
        case AnyCharacter:
        case CharacterRange:
          return true;
        default:
          return false;
        }
      },
      depth);
}

[[nodiscard]] inline bool
starts_with_synthetic_terminal_then_unassigned_lexical_tail(
    const grammar::AbstractElement &element,
    std::uint8_t depth = 0) noexcept {
  if (depth > 24u) {
    return false;
  }
  switch (element.getKind()) {
  case ElementKind::DataTypeRule:
    return starts_with_synthetic_terminal_then_unassigned_lexical_tail(
        *static_cast<const grammar::DataTypeRule &>(element).getElement(),
        static_cast<std::uint8_t>(depth + 1u));
  case ElementKind::TerminalRule:
    return starts_with_synthetic_terminal_then_unassigned_lexical_tail(
        *static_cast<const grammar::TerminalRule &>(element).getElement(),
        static_cast<std::uint8_t>(depth + 1u));
  case ElementKind::Group: {
    const auto &group = static_cast<const grammar::Group &>(element);
    std::size_t i = 0;
    for (; i < group.size(); ++i) {
      const auto &child = *group.get(i);
      if (child.isNullable()) {
        continue;
      }
      if (!starts_with_insertable_terminal(child)) {
        return false;
      }
      ++i;
      break;
    }
    for (; i < group.size(); ++i) {
      const auto &child = *group.get(i);
      if (child.isNullable()) {
        continue;
      }
      return is_unassigned_lexical_tail(
          child, static_cast<std::uint8_t>(depth + 1u));
    }
    return false;
  }
  case ElementKind::Assignment:
  case ElementKind::ParserRule:
  case ElementKind::OrderedChoice:
  case ElementKind::Repetition:
  case ElementKind::Literal:
  case ElementKind::AndPredicate:
  case ElementKind::AnyCharacter:
  case ElementKind::CharacterRange:
  case ElementKind::Create:
  case ElementKind::InfixOperator:
  case ElementKind::InfixRule:
  case ElementKind::Nest:
  case ElementKind::NotPredicate:
  case ElementKind::UnorderedGroup:
    return false;
  }
  return false;
}

[[nodiscard]] inline bool
starts_with_required_literal_anchor(const grammar::AbstractElement &element,
                                    std::uint8_t depth = 0) noexcept {
  if (depth > 24u) {
    return false;
  }
  switch (element.getKind()) {
  case ElementKind::Literal:
    return !static_cast<const grammar::Literal &>(element).getValue().empty();
  case ElementKind::Assignment:
    return starts_with_required_literal_anchor(
        *static_cast<const grammar::Assignment &>(element).getElement(),
        static_cast<std::uint8_t>(depth + 1u));
  case ElementKind::TerminalRule:
    return starts_with_required_literal_anchor(
        *static_cast<const grammar::TerminalRule &>(element).getElement(),
        static_cast<std::uint8_t>(depth + 1u));
  case ElementKind::ParserRule:
    return starts_with_required_literal_anchor(
        *static_cast<const grammar::ParserRule &>(element).getElement(),
        static_cast<std::uint8_t>(depth + 1u));
  case ElementKind::DataTypeRule:
    return starts_with_required_literal_anchor(
        *static_cast<const grammar::DataTypeRule &>(element).getElement(),
        static_cast<std::uint8_t>(depth + 1u));
  case ElementKind::Group: {
    const auto &group = static_cast<const grammar::Group &>(element);
    for (std::size_t i = 0; i < group.size(); ++i) {
      const auto &child = *group.get(i);
      if (starts_with_required_literal_anchor(
              child, static_cast<std::uint8_t>(depth + 1u))) {
        return true;
      }
      if (!child.isNullable()) {
        return false;
      }
    }
    return false;
  }
  case ElementKind::OrderedChoice: {
    const auto &choice = static_cast<const grammar::OrderedChoice &>(element);
    if (choice.size() == 0u) {
      return false;
    }
    for (std::size_t i = 0; i < choice.size(); ++i) {
      if (!starts_with_required_literal_anchor(
              *choice.get(i), static_cast<std::uint8_t>(depth + 1u))) {
        return false;
      }
    }
    return true;
  }
  case ElementKind::Repetition: {
    const auto &repetition =
        static_cast<const grammar::Repetition &>(element);
    return repetition.getMin() > 0u &&
           starts_with_required_literal_anchor(
               *repetition.getElement(),
               static_cast<std::uint8_t>(depth + 1u));
  }
  case ElementKind::AndPredicate:
  case ElementKind::AnyCharacter:
  case ElementKind::CharacterRange:
  case ElementKind::Create:
  case ElementKind::InfixOperator:
  case ElementKind::InfixRule:
  case ElementKind::Nest:
  case ElementKind::NotPredicate:
  case ElementKind::UnorderedGroup:
    return false;
  }
  return false;
}

/// Pure-grammar facts derived from a `Repetition`'s element once per instance,
/// cached so each iteration boundary reads a single field.
struct ShapeFacts {
  bool hasInsertableTerminal = false;
  bool hasRequiredLiteralAnchor = false;
  bool hasSyntheticTerminalThenLexicalTail = false;
};

} // namespace pegium::parser::detail
