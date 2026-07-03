#include <pegium/core/parser/SequenceRecoveryEngine.hpp>

#include <cstdint>
#include <optional>
#include <utility>

#include <pegium/core/parser/CandidateEnvelope.hpp>
#include <pegium/core/parser/ContextShared.hpp>
#include <pegium/core/parser/EditableRecoverySupport.hpp>
#include <pegium/core/parser/RecoveryCandidate.hpp>
#include <pegium/core/parser/RecoveryUtils.hpp>

namespace pegium::parser::detail {

namespace {

struct SequenceRecoveryReplayPlan {
  bool valid = false;
  bool insertCurrentTerminal = false;
  std::uint32_t currentTerminalDeleteRunCount = 0u;
  bool currentTerminalDeleteRunEndsAfterHiddenTriviaExtension = false;
  bool reparseCurrentWithoutDelete = false;
  bool skipNullable = false;
  bool preferTailEntryInsert = false;
  bool allowNullableTailStop = false;
};

struct TerminalDeleteRunObservation {
  std::uint32_t deleteCount = 0u;
  bool endsAfterHiddenTriviaExtension = false;
};

struct SequenceFacts {
  bool suffixFullyNullable = false;
  bool strictSuffixStartsAtCurrentCursor = false;
  bool recoverableSuffixStartsAtCurrentCursor = false;
  bool currentOffsetWithinRecoveryWindow = false;
};

[[nodiscard]] constexpr bool
element_is_terminal_like_for_missing_insert(ElementKind kind) noexcept {
  return kind == ElementKind::Literal || kind == ElementKind::TerminalRule;
}

void consider_sequence_recovery_candidate(
    SequenceRecoveryReplayPlan &bestPlan,
    EditableRecoveryCandidate &bestCandidate,
    const SequenceRecoveryReplayPlan &candidatePlan,
    const EditableRecoveryCandidate &candidate) noexcept {
  // Routes both envelopes through the shared `consider_candidate_envelope`
  // kernel with `FamilyRedundancyPolicy::RankOnly`: Group has no
  // family-redundancy step (each plan is its own family), so the pipeline
  // reduces to admission + `is_better_recovery_key` ranking on the
  // candidate-derived `RecoveryKey`.
  const auto candidateEnvelope = to_candidate_envelope(candidate);
  const auto bestEnvelope = to_candidate_envelope(bestCandidate);
  if (consider_candidate_envelope(candidateEnvelope, bestEnvelope,
                                  bestCandidate.matched,
                                  FamilyRedundancyPolicy::RankOnly) ==
      SelectionVerdict::Take) {
    bestPlan = candidatePlan;
    bestCandidate = candidate;
  }
}

[[nodiscard]] SequenceFacts
build_sequence_facts(RecoveryContext &ctx, const SequenceElementOps &ops) {
  const auto suffixEntryFacts = ops.analyzeSuffixEntryFacts(ops.self, ctx);
  return {
      .suffixFullyNullable = ops.suffixFullyNullable,
      .strictSuffixStartsAtCurrentCursor =
          suffixEntryFacts.strictStartsAtCurrentCursor,
      .recoverableSuffixStartsAtCurrentCursor =
          suffixEntryFacts.recoverableStartsAtCurrentCursor,
      .currentOffsetWithinRecoveryWindow =
          ctx.hasPendingRecoveryWindows() &&
          (ctx.cursorOffset() <= ctx.pendingRecoveryWindowMaxCursorOffset() ||
           ctx.isInRecoveryPhase()),
  };
}

[[nodiscard]] bool terminal_allows_nullable_tail_stop(
    const TerminalRecoveryState &state,
    const SequenceFacts &sequenceFacts) noexcept {
  return state.facts.triviaGap.hasHiddenGap ||
         sequenceFacts.suffixFullyNullable;
}

[[nodiscard]] std::pair<bool, bool> build_terminal_sequence_legality(
    RecoveryContext &ctx, const SequenceElementOps &ops,
    const TerminalRecoveryState &state, const SequenceFacts &sequenceFacts) {
  const bool cursorStartsVisibleSource = cursor_starts_visible_source(ctx);
  const bool sequenceLiteralInsertHasContinuation =
      ctx.cursor() >= ctx.end || sequenceFacts.suffixFullyNullable ||
      sequenceFacts.strictSuffixStartsAtCurrentCursor ||
      sequenceFacts.recoverableSuffixStartsAtCurrentCursor;
  const bool currentAllowsSyntheticInsert = ops.allowsSyntheticInsertCurrent(
      ops.self, ctx, sequenceLiteralInsertHasContinuation);
  const bool currentSiteWithinRecoveryWindow =
      sequenceFacts.currentOffsetWithinRecoveryWindow ||
      state.skippedFromRecoveryWindowBeforeCurrent;
  const bool hasLocalSequenceEdits =
      ctx.currentEditCount() != state.sequenceEditCountBase;
  const bool scopedLeadingInitialInsertAllowed =
      ops.isFirstElement && ctx.currentWindowEditCount() == 0u &&
      ctx.allowsScopedLeadingTerminalInsertRecovery();
  const bool visibleSourceAvailableForScopedContinuation =
      cursorStartsVisibleSource ||
      state.facts.triviaGap.visibleSourceAfterLocalSkip ||
      cursor_reaches_visible_source_after_local_skip(ctx);
  const bool recoveredPrefixImmediateInsertAllowed =
      ((state.hasRecoveredPrefixBeforeCurrent &&
        currentSiteWithinRecoveryWindow) ||
       (state.hasRecoveredPrefixBeforeCurrent &&
        sequenceFacts.suffixFullyNullable &&
        (state.facts.triviaGap.hasHiddenGap || cursorStartsVisibleSource ||
         ctx.cursor() >= ctx.end))) &&
      (state.facts.triviaGap.hasHiddenGap || !hasLocalSequenceEdits);
  const bool scopedLeadingContinuationInsertAllowed =
      ops.isFirstElement && ctx.allowsScopedLeadingTerminalInsertRecovery() &&
      !ctx.editPolicy.allowDelete &&
      sequenceFacts.currentOffsetWithinRecoveryWindow &&
      visibleSourceAvailableForScopedContinuation;
  const bool committedPrefixContinuationInsertAllowed =
      state.facts.allowStructuredVisibleContinuationInsert &&
      !hasLocalSequenceEdits && visibleSourceAvailableForScopedContinuation &&
      (sequenceFacts.strictSuffixStartsAtCurrentCursor ||
       sequenceFacts.recoverableSuffixStartsAtCurrentCursor);
  const bool continuedRecoveredPrefixInsertAllowed =
      state.hasRecoveredPrefixBeforeCurrent &&
      ctx.currentWindowEditCount() > 0u && !hasLocalSequenceEdits &&
      visibleSourceAvailableForScopedContinuation &&
      sequenceFacts.strictSuffixStartsAtCurrentCursor &&
      (!ops.isFirstElement ||
       (!ctx.editPolicy.allowDelete &&
        ctx.allowsScopedLeadingTerminalInsertRecovery()));
  const bool currentTerminalInsertionAllowed =
      currentAllowsSyntheticInsert && !state.facts.localRecoveryBlocked;
  return {
      currentTerminalInsertionAllowed &&
          (recoveredPrefixImmediateInsertAllowed ||
           continuedRecoveredPrefixInsertAllowed ||
           scopedLeadingInitialInsertAllowed ||
           scopedLeadingContinuationInsertAllowed ||
           committedPrefixContinuationInsertAllowed) &&
          (sequenceFacts.suffixFullyNullable ||
           sequenceFacts.strictSuffixStartsAtCurrentCursor ||
           sequenceFacts.recoverableSuffixStartsAtCurrentCursor),
      currentTerminalInsertionAllowed && !ops.isFirstElement &&
          sequenceFacts.suffixFullyNullable &&
          state.hasRecoveredPrefixBeforeCurrent &&
          currentSiteWithinRecoveryWindow &&
          !(cursorStartsVisibleSource && ctx.currentWindowEditCount() > 0u),
  };
}

[[nodiscard]] bool
current_recoverable_entry_signal(RecoveryContext &ctx,
                                 const SequenceElementOps &ops,
                                 const RecoveryContext::Checkpoint &checkpoint) {
  ctx.rewind(checkpoint);
  ProbeRestoreScope guard{ctx};
  return ops.withCurrentFollow(ops.self, ctx, [&]() {
    return ops.fastProbeCurrent(ops.self, ctx) ||
           ops.probeCurrentAtEntry(ops.self, ctx) ||
           ops.probeCurrentLocallyRecoverable(ops.self, ctx);
  });
}

[[nodiscard]] std::pair<bool, bool> build_current_failure_terminal_legality(
    RecoveryContext &ctx, const SequenceElementOps &ops,
    const RecoveryContext::Checkpoint &checkpoint,
    const TerminalRecoveryState &terminalRecoveryState,
    const SequenceFacts &terminalEntryFacts) {
  if (ops.currentKind == ElementKind::Literal ||
      ops.currentKind == ElementKind::TerminalRule) {
    ctx.rewind(checkpoint);
    return build_terminal_sequence_legality(ctx, ops, terminalRecoveryState,
                                            terminalEntryFacts);
  }
  return {};
}

bool replay_skip_nullable_attempt(RecoveryContext &ctx,
                                  const SequenceElementOps &ops,
                                  bool preferTailEntryInsert = false) {
  const auto parseTail = [&ops, &ctx]() {
    return ops.parseTail(ops.self, ctx, false);
  };
  const auto skipOrParseCurrentThenTail = [&]() {
    return ops.currentAllowsRecoverySkip
               ? parseTail()
               : (sequence_parse_current_element(ctx, ops, {}) && parseTail());
  };
  if (!preferTailEntryInsert) {
    return skipOrParseCurrentThenTail();
  }
  auto noDeleteGuard =
      ctx.withEditPermissions(ctx.editPolicy.allowInsert, false);
  (void)noDeleteGuard;
  const auto *tailFirstRequired = ops.firstRequiredTailElement(ops.self);
  const bool tailStartsWithTerminal =
      tailFirstRequired != nullptr &&
      (tailFirstRequired->getKind() == ElementKind::Literal ||
       tailFirstRequired->getKind() == ElementKind::TerminalRule);
  if (!tailStartsWithTerminal) {
    return skipOrParseCurrentThenTail();
  }
  ScopedBoolOverride leadingInsertGuard{ctx.allowLeadingTerminalInsertScope,
                                        true};
  (void)leadingInsertGuard;
  return skipOrParseCurrentThenTail();
}

[[nodiscard]] EditableRecoveryCandidate evaluate_skip_nullable_attempt(
    RecoveryContext &ctx, const SequenceElementOps &ops,
    const RecoveryContext::Checkpoint &checkpoint,
    bool preferTailEntryInsert = false) {
  return evaluate_editable_recovery_candidate(
      ctx, checkpoint, checkpoint.recoveryState.editBudget.editCost,
      checkpoint.recoveryState.editBudget.editCount,
      [&ctx, &ops, preferTailEntryInsert]() {
        return replay_skip_nullable_attempt(ctx, ops, preferTailEntryInsert);
      });
}

bool replay_current_without_delete_attempt(
    RecoveryContext &ctx, const SequenceElementOps &ops,
    const TerminalRecoveryFacts &terminalRecoveryFacts,
    bool allowDeleteRecovery) {
  if (allowDeleteRecovery) {
    return sequence_parse_current_element(ctx, ops, terminalRecoveryFacts) &&
           ops.parseTail(ops.self, ctx, false);
  }
  {
    auto noDeleteGuard =
        ctx.withEditPermissions(ctx.editPolicy.allowInsert, false);
    (void)noDeleteGuard;
    if (!sequence_parse_current_element(ctx, ops, terminalRecoveryFacts)) {
      return false;
    }
  }
  return ops.parseTail(ops.self, ctx, false);
}

[[nodiscard]] EditableRecoveryCandidate evaluate_current_without_delete_attempt(
    RecoveryContext &ctx, const SequenceElementOps &ops,
    const RecoveryContext::Checkpoint &checkpoint,
    const TerminalRecoveryFacts &terminalRecoveryFacts,
    bool allowDeleteRecovery) {
  return evaluate_editable_recovery_candidate(
      ctx, checkpoint, checkpoint.recoveryState.editBudget.editCost,
      checkpoint.recoveryState.editBudget.editCount,
      [&ctx, &ops, &terminalRecoveryFacts, allowDeleteRecovery]() {
        return replay_current_without_delete_attempt(
            ctx, ops, terminalRecoveryFacts, allowDeleteRecovery);
      });
}

bool replay_tail_after_current_attempt(RecoveryContext &ctx,
                                       const SequenceElementOps &ops) {
  auto noDeleteGuard =
      ctx.withEditPermissions(ctx.editPolicy.allowInsert, false);
  (void)noDeleteGuard;
  return ops.parseTail(ops.self, ctx, false);
}

bool try_insert_current_terminal_with_clean_tail(RecoveryContext &ctx,
                                                 const SequenceElementOps &ops,
                                                 bool allowNullableTailStop) {
  const auto checkpoint = ctx.mark();
  const auto parseStartOffset = ctx.cursorOffset();
  auto allowLocalInsertGuard =
      ctx.withEditPermissions(true, ctx.editPolicy.allowDelete);
  (void)allowLocalInsertGuard;
  if (const bool inserted = ctx.insertSynthetic(ops.current); !inserted) {
    ctx.rewind(checkpoint);
    return false;
  }
  ctx.leaf(ctx.cursor(), ops.current, false, true);
  const auto editCountAfterCurrent = ctx.recoveryEditCount();
  const auto editCostAfterCurrent = ctx.currentEditCost();
  if (const bool tailMatched = ops.parseCleanTailNoEdits(ops.self, ctx);
      !tailMatched || ctx.recoveryEditCount() != editCountAfterCurrent ||
      ctx.currentEditCost() != editCostAfterCurrent) {
    ctx.rewind(checkpoint);
    return false;
  }
  const auto postMatchCheckpoint = ctx.mark();
  ctx.skip();
  const auto postSkipCursorOffset = ctx.cursorOffset();
  ctx.rewind(postMatchCheckpoint);
  if (!allowNullableTailStop &&
      postSkipCursorOffset <= parseStartOffset) {
    ctx.rewind(checkpoint);
    return false;
  }
  return true;
}

[[nodiscard]] std::optional<TerminalDeleteRunObservation>
observe_current_terminal_delete_run(RecoveryContext &ctx) {
  const auto checkpoint = ctx.mark();
  const bool previousSkipAfterDelete = ctx.skipAfterDelete;
  const char *const cursorStart = ctx.cursor();
  ctx.skipAfterDelete = false;
  std::optional<TerminalDeleteRunObservation> observation;
  (void)visit_guarded_delete_scan_positions(
      ctx,
      [&ctx, cursorStart]() noexcept {
        return allows_extended_terminal_delete_scan_match(ctx, cursorStart,
                                                          ctx.cursor());
      },
      [&observation](const DeleteScanVisitState &state) {
        observation = {
            .deleteCount = state.deleteCount,
            .endsAfterHiddenTriviaExtension = state.hiddenTriviaExtended,
        };
        return DeleteScanVisitResult::Continue;
      },
      {.extendThroughHiddenTrivia = true,
       .stopAtHiddenTriviaBoundary = true,
       .visitAfterHiddenTriviaExtension = true});
  ctx.skipAfterDelete = previousSkipAfterDelete;
  ctx.rewind(checkpoint);
  return observation;
}

bool replay_current_terminal_delete_run(
    RecoveryContext &ctx, const TerminalDeleteRunObservation &observation) {
  if (observation.deleteCount == 0u) {
    return true;
  }
  const bool previousSkipAfterDelete = ctx.skipAfterDelete;
  const char *const cursorStart = ctx.cursor();
  ctx.skipAfterDelete = false;
  const bool replayed =
      visit_guarded_delete_scan_positions(
          ctx,
          [&ctx, cursorStart]() noexcept {
            return allows_extended_terminal_delete_scan_match(ctx, cursorStart,
                                                              ctx.cursor());
          },
          [&observation](const DeleteScanVisitState &state) {
            if (state.deleteCount == observation.deleteCount &&
                state.hiddenTriviaExtended ==
                    observation.endsAfterHiddenTriviaExtension) {
              return DeleteScanVisitResult::Accept;
            }
            return DeleteScanVisitResult::Continue;
          },
          {.scan = {.maxDeletes = observation.deleteCount},
           .extendThroughHiddenTrivia = true,
           .stopAtHiddenTriviaBoundary = true,
           .visitAfterHiddenTriviaExtension = true}) ==
      DeleteScanVisitResult::Accept;
  ctx.skipAfterDelete = previousSkipAfterDelete;
  return replayed;
}

bool replay_insert_current_terminal_attempt(
    RecoveryContext &ctx, const SequenceElementOps &ops,
    const TerminalDeleteRunObservation &deleteRun,
    bool allowNullableTailStop) {
  const auto checkpoint = ctx.mark();
  if (!replay_current_terminal_delete_run(ctx, deleteRun)) {
    ctx.rewind(checkpoint);
    return false;
  }
  if (try_insert_current_terminal_with_clean_tail(ctx, ops,
                                                  allowNullableTailStop)) {
    return true;
  }
  ctx.rewind(checkpoint);
  return false;
}

void consider_terminal_current_failure_attempts(
    SequenceRecoveryReplayPlan &bestPlan,
    EditableRecoveryCandidate &bestCandidate, RecoveryContext &ctx,
    const SequenceElementOps &ops, bool allowInsertCurrentTerminal,
    bool allowInsertCurrentAfterDeleteRun, bool allowNullableTailStop) {
  if (!allowInsertCurrentTerminal && !allowInsertCurrentAfterDeleteRun) {
    return;
  }
  const auto checkpoint = ctx.mark();
  const auto baseEditCost = ctx.currentEditCost();
  const auto baseRecoveryEditCount = ctx.recoveryEditCount();
  if (allowInsertCurrentTerminal) {
    consider_sequence_recovery_candidate(
        bestPlan, bestCandidate,
        {.valid = true,
         .insertCurrentTerminal = true,
         .allowNullableTailStop = allowNullableTailStop},
        evaluate_editable_recovery_candidate(
            ctx, checkpoint, baseEditCost, baseRecoveryEditCount,
            [&ctx, &ops, allowNullableTailStop]() {
              return replay_insert_current_terminal_attempt(
                  ctx, ops, {}, allowNullableTailStop);
            }));
  }
  if (allowInsertCurrentAfterDeleteRun) {
    const auto deleteRun = observe_current_terminal_delete_run(ctx);
    if (!deleteRun.has_value()) {
      return;
    }
    consider_sequence_recovery_candidate(
        bestPlan, bestCandidate,
        {.valid = true,
         .insertCurrentTerminal = true,
         .currentTerminalDeleteRunCount = deleteRun->deleteCount,
         .currentTerminalDeleteRunEndsAfterHiddenTriviaExtension =
             deleteRun->endsAfterHiddenTriviaExtension,
         .allowNullableTailStop = allowNullableTailStop},
        evaluate_editable_recovery_candidate(
            ctx, checkpoint, baseEditCost, baseRecoveryEditCount,
            [&ctx, &ops, deleteRun, allowNullableTailStop]() {
              return replay_insert_current_terminal_attempt(
                  ctx, ops, *deleteRun, allowNullableTailStop);
            }));
  }
}

// Shared by the current-failure and tail-failure paths. The tail path passes
// allowInsert*=false (no terminal attempts), allowCurrentWithoutDeleteAttempt
// =true (unconditional reparse) and allowDeleteRecovery=false.
SequenceRecoveryReplayPlan select_current_failure_sequence_attempt(
    RecoveryContext &ctx, const SequenceElementOps &ops,
    const RecoveryContext::Checkpoint &checkpoint,
    const TerminalRecoveryFacts &facts,
    bool allowInsertCurrentTerminal,
    bool allowInsertCurrentAfterDeleteRun,
    bool allowCurrentWithoutDeleteAttempt,
    bool allowSkipNullableAttempt,
    bool preferTailEntryInsert,
    bool allowTerminalNullableTailStop,
    bool allowDeleteRecovery) {
  SequenceRecoveryReplayPlan bestPlan{};
  EditableRecoveryCandidate bestCandidate{};
  if (allowInsertCurrentTerminal || allowInsertCurrentAfterDeleteRun) {
    ctx.rewind(checkpoint);
    consider_terminal_current_failure_attempts(
        bestPlan, bestCandidate, ctx, ops, allowInsertCurrentTerminal,
        allowInsertCurrentAfterDeleteRun, allowTerminalNullableTailStop);
  }
  if (ops.currentNullable) {
    if (allowCurrentWithoutDeleteAttempt) {
      ctx.rewind(checkpoint);
      consider_sequence_recovery_candidate(
          bestPlan, bestCandidate,
          {.valid = true,
           .reparseCurrentWithoutDelete = true},
          evaluate_current_without_delete_attempt(ctx, ops, checkpoint, facts,
                                                  allowDeleteRecovery));
    }
    if (allowSkipNullableAttempt) {
      ctx.rewind(checkpoint);
      consider_sequence_recovery_candidate(
          bestPlan, bestCandidate,
          {.valid = true,
           .skipNullable = true,
           .preferTailEntryInsert = preferTailEntryInsert},
          evaluate_skip_nullable_attempt(ctx, ops, checkpoint,
                                         preferTailEntryInsert));
    }
  }
  return bestPlan;
}

// Shared by the current-failure and tail-failure paths. A tail plan never
// sets insertCurrentTerminal and passes allowDeleteRecovery=false; the
// current-failure path passes allowDeleteRecovery=true.
bool replay_current_failure_sequence_attempt(
    RecoveryContext &ctx, const SequenceElementOps &ops,
    const TerminalRecoveryFacts &facts,
    const SequenceRecoveryReplayPlan &plan, bool allowDeleteRecovery) {
  if (!plan.valid) {
    return false;
  }
  if (plan.insertCurrentTerminal) {
    return replay_insert_current_terminal_attempt(
        ctx, ops,
        {.deleteCount = plan.currentTerminalDeleteRunCount,
         .endsAfterHiddenTriviaExtension =
             plan.currentTerminalDeleteRunEndsAfterHiddenTriviaExtension},
        plan.allowNullableTailStop);
  }
  if (plan.reparseCurrentWithoutDelete) {
    return replay_current_without_delete_attempt(ctx, ops, facts,
                                                 allowDeleteRecovery);
  }
  if (plan.skipNullable) {
    return replay_skip_nullable_attempt(ctx, ops, plan.preferTailEntryInsert);
  }
  return false;
}

} // namespace

bool sequence_select_missing_element_insert_replay(
    RecoveryContext &ctx, const SequenceElementOps &ops) {
  if (ops.currentNullable) {
    return false;
  }
  if (element_is_terminal_like_for_missing_insert(ops.currentKind)) {
    return false;
  }
  if (const auto *inner = ops.currentInnerElement(ops.self);
      inner != nullptr) {
    if (inner->getKind() == ElementKind::Literal) {
      return false;
    }
  }
  if (const bool currentMatchesStrict =
          ops.parseCurrentWithoutSideEffects(ops.self, ctx)) {
    // When `current` is the last element, analyze_suffix_entry_facts<count>
    // returns {} (strictStartsAtCurrentCursor=false), so this also covers
    // the "nothing follows" case without a separate guard.
    const auto suffixEntryFacts = ops.analyzeSuffixEntryFacts(ops.self, ctx);
    if (!suffixEntryFacts.strictStartsAtCurrentCursor) {
      return false;
    }
  }
  const auto checkpoint = ctx.mark();
  const auto baseEditCost = ctx.currentEditCost();
  const auto baseRecoveryEditCount = ctx.recoveryEditCount();
  bool insertMatchedCleanTail = false;
  const auto insertCandidate = evaluate_editable_recovery_candidate(
      ctx, checkpoint, baseEditCost, baseRecoveryEditCount,
      [&ctx, &ops, &insertMatchedCleanTail]() {
        return sequence_replay_insert_missing_element_attempt(
            ctx, ops, insertMatchedCleanTail);
      });
  if (!insertCandidate.matched ||
      insertCandidate.postSkipCursorOffset <=
          insertCandidate.firstEditOffset) {
    return false;
  }
  const bool singleInsertedElementWithCleanTail =
      insertMatchedCleanTail && insertCandidate.editCount == 1u;
  const auto parseCandidate = evaluate_editable_recovery_candidate(
      ctx, checkpoint, baseEditCost, baseRecoveryEditCount,
      [&ctx, &ops, singleInsertedElementWithCleanTail]() {
        const auto editGuard =
            singleInsertedElementWithCleanTail
                ? ctx.withEditPermissions(ctx.editPolicy.allowInsert, false)
                : ctx.withEditPermissions(ctx.editPolicy.allowInsert,
                                          ctx.editPolicy.allowDelete);
        (void)editGuard;
        return sequence_parse_current_element(ctx, ops, {}) &&
               ops.parseTail(ops.self, ctx, false);
      });
  const bool selectInsertReplay =
      !parseCandidate.matched ||
      is_better_recovery_key(editable_recovery_key(insertCandidate),
                             editable_recovery_key(parseCandidate));
  return selectInsertReplay;
}

bool sequence_replay_insert_missing_element_attempt(
    RecoveryContext &ctx, const SequenceElementOps &ops,
    bool &matchedCleanTail) {
  matchedCleanTail = false;
  if (!ctx.insertSynthetic(ops.current)) {
    return false;
  }
  if (ops.parseCleanTailNoEdits(ops.self, ctx)) {
    matchedCleanTail = true;
    return true;
  }
  if (!ops.tailSupportsSingleTerminalRecovery(ops.self)) {
    return false;
  }
  bool tailRecoverable = false;
  {
    ProbeRestoreScope guard{ctx};
    tailRecoverable = ops.suffixProbeRecoverableEntry(ops.self, ctx);
  }
  return tailRecoverable && ops.parseTail(ops.self, ctx, false);
}

TerminalRecoveryState sequence_build_terminal_recovery_state(
    const RecoveryContext &ctx, const SequenceElementOps &ops,
    const RecoveryContext::Checkpoint &checkpoint, const char *cursorBeforeSkip,
    bool previousNullableSiblingOwnsCursor) {
  const bool hasVisiblePrefixCursor =
      cursorBeforeSkip != nullptr &&
      (cursorBeforeSkip > ctx.begin ||
       ctx.lastVisibleCursorOffset() >=
           static_cast<TextOffset>(cursorBeforeSkip - ctx.begin));
  const bool hasCommittedVisiblePrefixBeforeCurrent =
      hasVisiblePrefixCursor || ctx.lastVisibleCursorOffset() > 0u;
  const bool hasRecoveredPrefixBeforeCurrent =
      hasVisiblePrefixCursor || checkpoint.recoveryState.editBudget.hadEdits;
  const char *const lastVisibleCursor =
      ctx.begin + ctx.lastVisibleCursorOffset();
  const char *triviaGapBegin = nullptr;
  if (cursorBeforeSkip != nullptr && ctx.cursor() > cursorBeforeSkip) {
    triviaGapBegin = cursorBeforeSkip;
  } else if (ctx.cursor() > lastVisibleCursor &&
             ctx.skip_without_builder(lastVisibleCursor) == ctx.cursor()) {
    triviaGapBegin = lastVisibleCursor;
  }
  auto triviaGap = TriviaGapProfile{};
  if (hasRecoveredPrefixBeforeCurrent) {
    triviaGap = make_trivia_gap_profile(ctx, triviaGapBegin, ctx.cursor());
  }
  const bool previousIsTerminalish = ops.previousIsTerminalish;
  const bool skippedFromRecoveryWindowBeforeCurrent =
      cursorBeforeSkip != nullptr && ctx.hasPendingRecoveryWindows() &&
      ctx.cursor() > cursorBeforeSkip &&
      ctx.skip_without_builder(cursorBeforeSkip) == ctx.cursor() &&
      static_cast<TextOffset>(cursorBeforeSkip - ctx.begin) <=
          ctx.pendingRecoveryWindowMaxCursorOffset();
  return {
      .facts =
          {
              .triviaGap = triviaGap,
              .previousElementIsTerminalish = previousIsTerminalish,
              .allowStructuredVisibleContinuationInsert =
                  hasCommittedVisiblePrefixBeforeCurrent,
              .localRecoveryBlocked =
                  previousNullableSiblingOwnsCursor,
          },
      .hasRecoveredPrefixBeforeCurrent = hasRecoveredPrefixBeforeCurrent,
      .skippedFromRecoveryWindowBeforeCurrent =
          skippedFromRecoveryWindowBeforeCurrent,
      .sequenceEditCountBase = checkpoint.recoveryState.editBudget.editCount,
  };
}

bool sequence_nullable_current_visible_repair_competes_with_strict_suffix(
    RecoveryContext &ctx, const SequenceElementOps &ops,
    bool strictSuffixStartsAtEntry) {
  if (!ops.currentNullable) {
    return false;
  }
  if (!strictSuffixStartsAtEntry) {
    return false;
  }
  ProbeRestoreScope guard{ctx};
  auto noFollowGuard = ctx.withNoFollowProbe();
  (void)noFollowGuard;
  return ops.probeCurrentAtEntryConsumesVisible(ops.self, ctx);
}

bool sequence_parse_competing_nullable_current_repair(
    RecoveryContext &ctx, const SequenceElementOps &ops,
    const TerminalRecoveryFacts &facts) {
  if (!ops.currentNullable) {
    return false;
  }
  auto noFollowGuard = ctx.withNoFollowProbe();
  (void)noFollowGuard;
  return ops.parseCurrentTerminalRecovery(ops.self, ctx, facts);
}

bool sequence_try_skip_nullable_transition(RecoveryContext &ctx,
                                           const SequenceElementOps &ops,
                                           bool nullableCurrentLooksStarted,
                                           bool strictSuffixStartsAtEntry) {
  if (!ops.currentNullable) {
    return false;
  }
  if (nullableCurrentLooksStarted || !strictSuffixStartsAtEntry) {
    return false;
  }
  const auto skipNullableCheckpoint = ctx.mark();
  const bool matched =
      ops.currentAllowsRecoverySkip
          ? ops.parseTail(ops.self, ctx, false)
          : (ops.parseCurrent(ops.self, ctx) &&
             ops.parseTail(ops.self, ctx, false));
  if (matched) {
    return true;
  }
  ctx.rewind(skipNullableCheckpoint);
  return false;
}

bool sequence_parse_current_element(RecoveryContext &ctx,
                                    const SequenceElementOps &ops,
                                    const TerminalRecoveryFacts &facts) {
  return ops.withCurrentFollow(ops.self, ctx, [&]() {
    return ops.parseCurrentTerminalRecovery(ops.self, ctx, facts);
  });
}

bool sequence_recover_after_current_parse_failure(
    RecoveryContext &ctx, const SequenceElementOps &ops,
    const RecoveryContext::Checkpoint &checkpoint,
    const TerminalRecoveryState &terminalRecoveryState,
    bool nullableCurrentLooksStarted) {
  const auto sequenceFacts = build_sequence_facts(ctx, ops);
  const auto [allowInsertCurrentTerminal, allowInsertCurrentAfterDeleteRun] =
      build_current_failure_terminal_legality(
          ctx, ops, checkpoint, terminalRecoveryState, sequenceFacts);
  const bool preferTailEntryInsert =
      sequenceFacts.recoverableSuffixStartsAtCurrentCursor;
  const bool allowTerminalNullableTailStop =
      terminal_allows_nullable_tail_stop(terminalRecoveryState, sequenceFacts);
  const bool allowSkipNullableAttempt =
      (!nullableCurrentLooksStarted ||
       sequenceFacts.strictSuffixStartsAtCurrentCursor ||
       sequenceFacts.recoverableSuffixStartsAtCurrentCursor) &&
      ops.currentKind != ElementKind::AndPredicate &&
      ops.currentKind != ElementKind::NotPredicate;
  bool allowCurrentWithoutDeleteAttempt = false;
  if (ops.currentNullable) {
    if (nullableCurrentLooksStarted) {
      allowCurrentWithoutDeleteAttempt =
          !allowSkipNullableAttempt ||
          current_recoverable_entry_signal(ctx, ops, checkpoint);
    }
  }
  if (!ops.currentNullable) {
    if (allowInsertCurrentTerminal && !allowInsertCurrentAfterDeleteRun) {
      ctx.rewind(checkpoint);
      if (replay_insert_current_terminal_attempt(
              ctx, ops, {}, allowTerminalNullableTailStop)) {
        return true;
      }
      ctx.rewind(checkpoint);
    }
  }
  const auto bestPlan = select_current_failure_sequence_attempt(
      ctx, ops, checkpoint, terminalRecoveryState.facts,
      allowInsertCurrentTerminal,
      allowInsertCurrentAfterDeleteRun,
      allowCurrentWithoutDeleteAttempt,
      allowSkipNullableAttempt, preferTailEntryInsert,
      allowTerminalNullableTailStop, /*allowDeleteRecovery=*/true);
  ctx.rewind(checkpoint);
  return replay_current_failure_sequence_attempt(
      ctx, ops, terminalRecoveryState.facts, bestPlan,
      /*allowDeleteRecovery=*/true);
}

bool sequence_recover_after_tail_parse_failure(
    RecoveryContext &ctx, const SequenceElementOps &ops,
    const RecoveryContext::Checkpoint &checkpoint,
    const RecoveryContext::Checkpoint &checkpointAfterCurrent,
    const TerminalRecoveryFacts &terminalRecoveryFacts) {
  if (!ops.currentNullable) {
    return false;
  }
  ctx.rewind(checkpointAfterCurrent);
  if (replay_tail_after_current_attempt(ctx, ops)) {
    return true;
  }
  ctx.rewind(checkpointAfterCurrent);
  const auto sequenceFacts = build_sequence_facts(ctx, ops);
  const bool recoverableSuffix =
      sequenceFacts.recoverableSuffixStartsAtCurrentCursor;
  // The tail path is the current-failure path minus terminal attempts, with
  // an unconditional reparse and allowDeleteRecovery=false.
  const auto bestPlan = select_current_failure_sequence_attempt(
      ctx, ops, checkpoint, terminalRecoveryFacts,
      /*allowInsertCurrentTerminal=*/false,
      /*allowInsertCurrentAfterDeleteRun=*/false,
      /*allowCurrentWithoutDeleteAttempt=*/true,
      /*allowSkipNullableAttempt=*/recoverableSuffix,
      /*preferTailEntryInsert=*/recoverableSuffix,
      /*allowTerminalNullableTailStop=*/false,
      /*allowDeleteRecovery=*/false);
  if (!bestPlan.valid) {
    ctx.rewind(checkpointAfterCurrent);
    return false;
  }
  ctx.rewind(checkpoint);
  if (replay_current_failure_sequence_attempt(
          ctx, ops, terminalRecoveryFacts, bestPlan,
          /*allowDeleteRecovery=*/false)) {
    return true;
  }
  ctx.rewind(checkpointAfterCurrent);
  return false;
}

} // namespace pegium::parser::detail
