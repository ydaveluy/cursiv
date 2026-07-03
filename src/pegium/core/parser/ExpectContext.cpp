#include <pegium/core/parser/ExpectContext.hpp>

#include <algorithm>
#include <utility>

#include <pegium/core/grammar/Assignment.hpp>
#include <pegium/core/grammar/Literal.hpp>

namespace pegium::parser {

void ExpectContext::restore_rule(
    ExpectContext &ctx,
    const grammar::AbstractElement *previous) noexcept {
  ctx._currentRule = static_cast<const grammar::AbstractRule *>(previous);
  ctx.popContextElement();
}

void ExpectContext::restore_assignment(
    ExpectContext &ctx,
    const grammar::AbstractElement *previous) noexcept {
  ctx._currentAssignment =
      static_cast<const grammar::Assignment *>(previous);
  ctx.popContextElement();
}

bool ExpectContext::insertSynthetic(const grammar::AbstractElement *) {
  if (!editPolicy.trackEditState || !canInsert() ||
      !canAffordEdit(ParseDiagnosticKind::Inserted)) {
    return false;
  }
  detail::apply_non_delete_edit_state(
      detail::default_edit_cost(ParseDiagnosticKind::Inserted),
      editBudget.editCost, editBudget.editCount, editBudget.hadEdits,
      editBudget.consecutiveDeletes);
  return true;
}

bool ExpectContext::insertSyntheticGapAt(const char *position,
                                         const char *message) {
  (void)message;
  if (!editPolicy.trackEditState || position < begin || position > anchor) {
    return false;
  }
  if (const auto offset = static_cast<TextOffset>(position - begin);
      !detail::can_insert(editPolicy.allowInsert, canEditAtOffset(offset)) ||
      !canAffordEdit(ParseDiagnosticKind::Inserted)) {
    return false;
  }
  detail::apply_non_delete_edit_state(
      detail::default_edit_cost(ParseDiagnosticKind::Inserted),
      editBudget.editCost, editBudget.editCount, editBudget.hadEdits,
      editBudget.consecutiveDeletes);
  return true;
}

bool ExpectContext::deleteOneCodepoint() noexcept {
  // The `_cursor >= anchor` guard is subsumed by canDelete() below, which
  // begins with the same check (and, unlike RecoveryContext, this context has
  // no clearPendingDeleteHiddenTriviaBridge side effect to run at the anchor).
  if (!editPolicy.trackEditState || !canDelete() ||
      !canAffordEdit(ParseDiagnosticKind::Deleted)) {
    return false;
  }
  const char *const next = detail::next_codepoint_cursor(cursor());
  if (next <= cursor() || next > anchor) {
    return false;
  }
  _cursor = next;
  if (_cursor > _maxCursor) {
    _maxCursor = _cursor;
  }
  detail::apply_delete_edit_state(
      detail::default_edit_cost(ParseDiagnosticKind::Deleted),
      editBudget.editCost, editBudget.editCount, editBudget.hadEdits,
      editBudget.consecutiveDeletes);
  skip();
  return true;
}

bool ExpectContext::replaceLeaf(const char *endPtr,
                                const grammar::AbstractElement *element,
                                std::uint32_t replacementCost, bool hidden,
                                std::uint32_t fuzzyOperationDistance) {
  (void)fuzzyOperationDistance;
  if (!editPolicy.trackEditState || endPtr <= cursor() || endPtr > anchor ||
      !canEdit() || !canAffordEdit(replacementCost)) {
    return false;
  }
  detail::apply_non_delete_edit_state(replacementCost, editBudget.editCost,
                                      editBudget.editCount, editBudget.hadEdits,
                                      editBudget.consecutiveDeletes);
  leaf(endPtr, element, hidden, true);
  return true;
}

void ExpectContext::addKeyword(const grammar::Literal *literal) {
  addFrontier(makePath(literal));
}

void ExpectContext::addRule(const grammar::AbstractRule *rule) {
  addFrontier(makePath(rule));
}

void ExpectContext::addReference(const grammar::Assignment *assignment) {
  addFrontier(makePath(assignment));
}

void ExpectContext::mergeFrontier(std::span<const ExpectPath> items) {
  if (items.empty()) {
    return;
  }
  for (const auto &item : items) {
    addFrontier(item);
  }
}

void ExpectContext::addFrontier(ExpectPath path) {
  if (!path.isValid()) {
    return;
  }
  if (frontier.empty()) [[likely]] {
    frontier.push_back(std::move(path));
    _frontierBlocked = true;
    return;
  }
  for (const auto &existing : frontier) {
    if (existing.elements.size() != path.elements.size()) {
      continue;
    }
    if (std::ranges::equal(
            existing.elements, path.elements,
            [this](const auto *lhs, const auto *rhs) {
              return sameContextElement(lhs, rhs);
            })) {
      _frontierBlocked = true;
      return;
    }
  }
  frontier.push_back(std::move(path));
  _frontierBlocked = true;
}

ExpectPath ExpectContext::makePath(
    const grammar::AbstractElement *leaf) const {
  ExpectPath path{.elements = _contextPath};
  if (leaf != nullptr &&
      (path.elements.empty() || path.elements.back() != leaf)) {
    path.elements.push_back(leaf);
  }
  return path;
}

void ExpectContext::pushContextElement(
    const grammar::AbstractElement *element) {
  if (element != nullptr) {
    _contextPath.push_back(element);
  }
}

void ExpectContext::popContextElement() noexcept {
  if (!_contextPath.empty()) {
    _contextPath.pop_back();
  }
}

bool ExpectContext::sameContextElement(
    const grammar::AbstractElement *lhs,
    const grammar::AbstractElement *rhs) const noexcept {
  if (lhs == rhs) {
    return true;
  }
  if (lhs == nullptr || rhs == nullptr || lhs->getKind() != rhs->getKind()) {
    return false;
  }
  using enum grammar::ElementKind;
  switch (lhs->getKind()) {
  case Literal:
    return static_cast<const grammar::Literal *>(lhs)->getValue() ==
           static_cast<const grammar::Literal *>(rhs)->getValue();
  case Assignment: {
    const auto *lhsAssignment = static_cast<const grammar::Assignment *>(lhs);
    const auto *rhsAssignment = static_cast<const grammar::Assignment *>(rhs);
    return lhsAssignment->getOperator() == rhsAssignment->getOperator() &&
           lhsAssignment->getFeature() == rhsAssignment->getFeature() &&
           lhsAssignment->isReference() == rhsAssignment->isReference() &&
           lhsAssignment->isMultiReference() ==
               rhsAssignment->isMultiReference() &&
           lhsAssignment->getType() == rhsAssignment->getType();
  }
  case DataTypeRule:
  case ParserRule:
  case TerminalRule:
  case InfixRule:
    return static_cast<const grammar::AbstractRule *>(lhs)->getName() ==
           static_cast<const grammar::AbstractRule *>(rhs)->getName();
  default:
    return false;
  }
}

} // namespace pegium::parser
