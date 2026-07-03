#include <pegium/core/parser/RecoveryContext.hpp>

#include <string_view>
#include <utility>

namespace pegium::parser {

void RecoveryContext::refreshRecoveryPhaseSlow() noexcept {
  auto &windowReplay = recoveryState.windowReplay;
  if (windowReplay.awaitingStrictStability && cursor() == end) {
    windowReplay.stableAfterRecovery = true;
    windowReplay.awaitingStrictStability = false;
    maybeDisableRecoveryBookkeeping();
  }

  if (!editWindow.has_value()) {
    return;
  }

  while (true) {
    if (windowReplay.inRecoveryPhase) {
      if (windowReplay.activeEditWindowCompleted) {
        windowReplay.inRecoveryPhase = false;
        continue;
      }
      if (windowReplay.currentForwardVisibleLeafCount >=
          active_window_forward_token_budget(*editWindow)) {
        completeActiveRecoveryWindow(false);
        continue;
      }
      return;
    }

    if (windowReplay.activeEditWindowCompleted) {
      return;
    }
    if (const auto recoveryBeginOffset = pendingRecoveryWindowBeginOffset();
        hasPendingCommittedRecoveryEdits() ||
        cursorOffset() < recoveryBeginOffset) {
      return;
    }
    beginActiveRecoveryWindow();
    return;
  }
}

bool RecoveryContext::insertSynthetic(const grammar::AbstractElement *element) {
  if (!editPolicy.trackEditState) {
    return false;
  }
  const bool replayingCommittedInsert =
      matches_committed_insert(cursorOffset(), element, {});
  if (hasPendingCommittedRecoveryEdits() && !replayingCommittedInsert) {
    PEGIUM_RECOVERY_TRACE(
        "[rule] insert blocked pending committed offset=", cursorOffset(),
        " floor=", editPolicy.editFloorOffset);
    return false;
  }
  clearPendingDeleteHiddenTriviaBridge();
  if (const bool canInsertAtCursor =
          canInsert() || (editPolicy.allowInsert &&
                          allowsCompletedWindowInsertionClusterAtCursor());
      !canInsertAtCursor || !canAffordEdit(ParseDiagnosticKind::Inserted)) {
    PEGIUM_RECOVERY_TRACE("[rule] insert blocked offset=", cursorOffset(),
                          " floor=", editPolicy.editFloorOffset);
    return false;
  }
  recoveryEdits.push_back({.kind = ParseDiagnosticKind::Inserted,
                           .offset = cursorOffset(),
                           .beginOffset = cursorOffset(),
                           .endOffset = cursorOffset(),
                           .element = element});
  detail::apply_non_delete_edit_state(
      detail::default_edit_cost(ParseDiagnosticKind::Inserted),
      recoveryState.editBudget.editCost, recoveryState.editBudget.editCount,
      recoveryState.editBudget.hadEdits,
      recoveryState.editBudget.consecutiveDeletes);
  if (replayingCommittedInsert) {
    consumeCommittedRecoveryEdit();
  }
  noteReplayForwardRequirementForCurrentWindow(ParseDiagnosticKind::Inserted);
  PEGIUM_STEP_TRACE_INC(detail::StepCounter::ParseContextInsert);
  PEGIUM_RECOVERY_TRACE("[rule] insert synthetic offset=", cursorOffset(),
                        " kind=", static_cast<int>(element->getKind()));
  return true;
}

bool RecoveryContext::insertSyntheticGapAt(const char *position,
                                           const char *message) {
  if (!editPolicy.trackEditState || position < begin || position > end) {
    return false;
  }
  const auto offset = static_cast<TextOffset>(position - begin);
  const std::string_view messageView =
      message == nullptr ? std::string_view{} : std::string_view{message};
  const bool replayingCommittedInsert =
      matches_committed_insert(offset, nullptr, messageView);
  if (hasPendingCommittedRecoveryEdits() && !replayingCommittedInsert) {
    PEGIUM_RECOVERY_TRACE(
        "[rule] insert synthetic gap blocked pending committed offset=", offset,
        " floor=", editPolicy.editFloorOffset);
    return false;
  }
  clearPendingDeleteHiddenTriviaBridge();
  if (!detail::can_insert(editPolicy.allowInsert, canEditAtOffset(offset)) ||
      !canAffordEdit(ParseDiagnosticKind::Inserted)) {
    PEGIUM_RECOVERY_TRACE("[rule] insert synthetic gap blocked offset=", offset,
                          " floor=", editPolicy.editFloorOffset);
    return false;
  }
  recoveryEdits.push_back({.kind = ParseDiagnosticKind::Inserted,
                           .offset = offset,
                           .beginOffset = offset,
                           .endOffset = offset,
                           .element = nullptr,
                           .message = message == nullptr
                                          ? std::string_view{}
                                          : std::string_view(message)});
  detail::apply_non_delete_edit_state(
      detail::default_edit_cost(ParseDiagnosticKind::Inserted),
      recoveryState.editBudget.editCost, recoveryState.editBudget.editCount,
      recoveryState.editBudget.hadEdits,
      recoveryState.editBudget.consecutiveDeletes);
  if (replayingCommittedInsert) {
    consumeCommittedRecoveryEdit();
  }
  noteReplayForwardRequirementForCurrentWindow(ParseDiagnosticKind::Inserted);
  PEGIUM_STEP_TRACE_INC(detail::StepCounter::ParseContextInsert);
  PEGIUM_RECOVERY_TRACE("[rule] insert synthetic gap offset=", offset);
  return true;
}

bool RecoveryContext::deleteOneCodepoint() noexcept {
  if (!editPolicy.trackEditState) {
    return false;
  }
  if (cursor() >= end) {
    clearPendingDeleteHiddenTriviaBridge();
    return false;
  }
  const auto *committedDelete = matching_committed_delete(cursorOffset());
  if (hasPendingCommittedRecoveryEdits() && committedDelete == nullptr) {
    clearPendingDeleteHiddenTriviaBridge();
    PEGIUM_RECOVERY_TRACE(
        "[rule] delete blocked pending committed offset=", cursorOffset(),
        " floor=", editPolicy.editFloorOffset);
    return false;
  }
  auto *mergedDeleteEdit = pendingHiddenTriviaDeleteEdit();
  if (!canDelete() ||
      destructiveEditOutsideActiveWindow(
          recoveryState.editBudget.consecutiveDeletes == 0u) ||
      !canAffordEdit(ParseDiagnosticKind::Deleted)) {
    clearPendingDeleteHiddenTriviaBridge();
    PEGIUM_RECOVERY_TRACE(
        "[rule] delete blocked offset=", cursorOffset(),
        " floor=", editPolicy.editFloorOffset,
        " consecutive=", recoveryState.editBudget.consecutiveDeletes, "/",
        editPolicy.maxConsecutiveCodepointDeletes);
    return false;
  }
  const auto beforeOffset = cursorOffset();
  (void)beforeOffset;
  const char *const deletedEnd = detail::next_codepoint_cursor(cursor());
  const char *const next = deletedEnd;
  if (next <= cursor()) [[unlikely]] {
    clearPendingDeleteHiddenTriviaBridge();
    return false;
  }
  if (mergedDeleteEdit != nullptr) {
    mergedDeleteEdit->endOffset =
        static_cast<TextOffset>(deletedEnd - begin);
    recoveryState.deleteBridge = {};
  } else {
    recoveryEdits.push_back({.kind = ParseDiagnosticKind::Deleted,
                             .offset = cursorOffset(),
                             .beginOffset = cursorOffset(),
                             .endOffset =
                                 static_cast<TextOffset>(deletedEnd - begin),
                             .element = nullptr});
  }
  detail::apply_delete_edit_state(
      detail::default_edit_cost(ParseDiagnosticKind::Deleted),
      recoveryState.editBudget.editCost, recoveryState.editBudget.editCount,
      recoveryState.editBudget.hadEdits,
      recoveryState.editBudget.consecutiveDeletes);
  noteReplayForwardRequirementForCurrentWindow(ParseDiagnosticKind::Deleted);
  PEGIUM_STEP_TRACE_INC(detail::StepCounter::ParseContextDelete);

  _cursor = next;
  if (_cursor > _maxCursor) {
    _maxCursor = _cursor;
  }
  if (skipAfterDelete) {
    skip();
  } else {
    refreshRecoveryPhase();
  }
  if (committedDelete != nullptr) {
    advanceCommittedDeleteReplay();
  }
  PEGIUM_RECOVERY_TRACE("[rule] delete offset=", beforeOffset, " -> ",
                        cursorOffset());
  return true;
}

bool RecoveryContext::extendLastDeleteThroughHiddenTrivia() noexcept {
  if (!editPolicy.trackEditState || recoveryEdits.empty() || cursor() >= end) {
    return false;
  }
  const auto *committedDelete = matching_committed_delete(cursorOffset());
  if (hasPendingCommittedRecoveryEdits() && committedDelete == nullptr) {
    return false;
  }
  clearPendingDeleteHiddenTriviaBridge();
  if (const auto &lastEdit = recoveryEdits.back();
      lastEdit.kind != ParseDiagnosticKind::Deleted ||
      lastEdit.endOffset != cursorOffset()) {
    return false;
  }

  const char *const hiddenEnd = skip_without_builder(cursor());
  if (hiddenEnd <= cursor()) {
    return false;
  }
  if (committedDelete != nullptr &&
      static_cast<TextOffset>(hiddenEnd - begin) > committedDelete->endOffset) {
    return false;
  }
  if (hiddenEnd >= end) {
    return false;
  }
  recoveryState.deleteBridge.pendingHiddenTriviaStart = cursor();
  recoveryState.deleteBridge.pendingHiddenTriviaEnd = hiddenEnd;
  _cursor = hiddenEnd;
  if (_cursor > _maxCursor) {
    _maxCursor = _cursor;
  }
  refreshRecoveryPhase();
  if (committedDelete != nullptr) {
    advanceCommittedDeleteReplay();
  }
  PEGIUM_RECOVERY_TRACE("[rule] bridge delete through hidden trivia -> ",
                        cursorOffset());
  return true;
}

} // namespace pegium::parser
