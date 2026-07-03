#pragma once

/// Public expectation-tracing result types: grammar derivation paths and the
/// frontier returned by `Parser::expect`.

#include <span>
#include <vector>

#include <pegium/core/grammar/AbstractElement.hpp>
#include <pegium/core/grammar/AbstractRule.hpp>
#include <pegium/core/grammar/Assignment.hpp>
#include <pegium/core/grammar/Literal.hpp>
#include <pegium/core/syntax-tree/CstNode.hpp>

namespace pegium::parser {
/// One expectation alternative as an ordered grammar derivation path.
///
/// The path runs from the outer parsing context to the concrete expected leaf
/// element. For example, a keyword expectation may look like:
/// `[Rule, Assignment, Literal]`.
struct ExpectPath {
  /// Ordered derivation path leading to the expected leaf element.
  ///
  /// Entries are always concrete grammar elements. The vector may be empty for
  /// an unresolved path, but it never stores null pointers.
  std::vector<const grammar::AbstractElement *> elements;

  /// Returns the full path as a lightweight read-only span.
  [[nodiscard]] std::span<const grammar::AbstractElement *const>
  view() const noexcept {
    return elements;
  }

  /// Returns the expected leaf element, or `nullptr` when the path is empty.
  [[nodiscard]] const grammar::AbstractElement *expectedElement() const noexcept {
    return elements.empty() ? nullptr : elements.back();
  }

  /// Returns the expected literal when the leaf is a keyword literal.
  [[nodiscard]] const grammar::Literal *literal() const noexcept {
    const auto *element = expectedElement();
    return element != nullptr &&
                   element->getKind() == grammar::ElementKind::Literal
               ? static_cast<const grammar::Literal *>(element)
               : nullptr;
  }

  /// Returns the expected rule when the leaf itself is a rule element.
  [[nodiscard]] const grammar::AbstractRule *expectedRule() const noexcept {
    const auto *element = expectedElement();
    return element != nullptr && isRuleElementKind(element->getKind())
               ? static_cast<const grammar::AbstractRule *>(element)
               : nullptr;
  }

  /// Returns the expected assignment when the leaf is an assignment.
  [[nodiscard]] const grammar::Assignment *expectedAssignment() const noexcept {
    const auto *element = expectedElement();
    return element != nullptr &&
                   element->getKind() == grammar::ElementKind::Assignment
               ? static_cast<const grammar::Assignment *>(element)
               : nullptr;
  }

  /// Returns the expected assignment when the leaf is a reference assignment.
  [[nodiscard]] const grammar::Assignment *
  expectedReferenceAssignment() const noexcept {
    const auto *assignment = expectedAssignment();
    return assignment != nullptr && assignment->isReference() ? assignment
                                                              : nullptr;
  }

  /// Returns the innermost rule present in the path.
  [[nodiscard]] const grammar::AbstractRule *contextRule() const noexcept {
    for (auto it = elements.rbegin(); it != elements.rend(); ++it) {
      if (isRuleElementKind((*it)->getKind())) {
        return static_cast<const grammar::AbstractRule *>(*it);
      }
    }
    return nullptr;
  }

  /// Returns the innermost assignment present in the path.
  [[nodiscard]] const grammar::Assignment *contextAssignment() const noexcept {
    for (auto it = elements.rbegin(); it != elements.rend(); ++it) {
      if ((*it)->getKind() == grammar::ElementKind::Assignment) {
        return static_cast<const grammar::Assignment *>(*it);
      }
    }
    return nullptr;
  }

  /// Returns whether the path expects a keyword literal.
  [[nodiscard]] bool expectsKeyword() const noexcept { return literal() != nullptr; }

  /// Returns whether the path expects a rule element.
  [[nodiscard]] bool expectsRule() const noexcept {
    return expectedRule() != nullptr;
  }

  /// Returns whether the path expects a reference assignment.
  [[nodiscard]] bool expectsReference() const noexcept {
    return expectedReferenceAssignment() != nullptr;
  }

  /// Returns whether the path has a concrete expected leaf element.
  [[nodiscard]] bool isValid() const noexcept { return expectedElement() != nullptr; }

private:
  [[nodiscard]] static constexpr bool
  isRuleElementKind(grammar::ElementKind kind) noexcept {
    using enum grammar::ElementKind;
    switch (kind) {
    case DataTypeRule:
    case ParserRule:
    case TerminalRule:
    case InfixRule:
      return true;
    default:
      return false;
    }
  }
};

/// Result of expectation tracing at one anchor offset.
struct ExpectResult {
  /// Alternative derivation paths that can continue parsing at `offset`.
  std::vector<ExpectPath> frontier;

  /// Requested anchor offset, clamped to the input size.
  TextOffset offset = 0;

  /// Whether the expectation search reached or explained the requested anchor.
  bool reachedAnchor = false;
};

} // namespace pegium::parser
