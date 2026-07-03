#include <pegium/core/parser/RepetitionRecoveryEngine.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>

#include <pegium/core/parser/CandidateEnvelope.hpp>
#include <pegium/core/parser/ContextShared.hpp>
#include <pegium/core/parser/EditableRecoverySupport.hpp>
#include <pegium/core/parser/RecoveryCandidate.hpp>
#include <pegium/core/parser/RecoveryTrace.hpp>
#include <pegium/core/parser/RecoveryUtils.hpp>
#include <pegium/core/parser/StepTrace.hpp>

namespace pegium::parser::detail {

namespace {

enum class IterationReplayKind : std::uint8_t {
  NoInsert,
  InsertRetry,
  DeleteRetry,
};

struct IterationObservation {
  /// The no-edit attempt's candidate. Repetition uses the shared
  /// `EditableRecoveryCandidate` carrier with `keyProgressOffset` set to the
  /// RAW iteration cursor (Invariant 2). The decision-relevant
  /// `CandidateEnvelope` view (key, family-redundancy filters) is projected on
  /// demand in `consider_iteration_attempt` rather than mirrored here.
  EditableRecoveryCandidate noInsertCandidate{};
  RecoveryProbeProgress noInsertProbe{};
  bool noInsertProgressed = false;
  bool startedWithoutEdits = false;
  bool iterationStarted = false;
};

struct IterationReplayPlan {
  IterationReplayKind kind = IterationReplayKind::NoInsert;
  bool legal = false;
  bool allowInsert = false;
  bool allowDelete = false;
  bool allowDestructiveWindowContinuation = false;
};

using IterationPlanList = std::array<IterationReplayPlan, 4>;

/// Who owns the next visible token at an iteration boundary (see
/// `decide_iteration_plans`): `Local` = the repeated element may recover;
/// `Parent` = the parent FOLLOW recovers + consumes visible at the same
/// cursor, so a weak/synthetic local start must defer (BOTH retry flags are
/// then forced false LAST).
enum class BoundaryOwnership : std::uint8_t { Local, Parent };

/// Layer-1 facts observed at an iteration boundary
/// (`observe_iteration_signals`) and consumed by `decide_iteration_plans`.
/// `stopCleanly` short-circuits the whole enumeration; when it is set the
/// boundary-ownership probes are NOT run and `boundaryOwnership` stays
/// `Local`.
struct IterationSignals {
  bool stopCleanly = false;
  bool noInsertCandidateLegal = false;
  bool firstZeroMinIteration = false;
  bool retryContinuesStartedIteration = false;
  bool noInsertProgressed = false;
  bool iterationStarted = false;
  bool speculativeSyntheticSingleLeafRetryAfterRecoveredIteration = false;
  BoundaryOwnership boundaryOwnership = BoundaryOwnership::Local;
};

struct IterationSelectionResult {
  EditableRecoveryCandidate bestCandidate{};
  IterationReplayPlan winningPlan{
      .kind = IterationReplayKind::NoInsert,
  };
};

/// Selects the better iteration attempt by routing both envelopes through
/// the shared `consider_candidate_envelope` kernel with
/// `FamilyRedundancyPolicy::IterationExtensionFamily`, mirroring
/// `consider_choice_attempt` (ChoiceAttempt.cpp). The decision-relevant
/// `CandidateEnvelope` is a pure projection of the candidate; both sides are
/// projected here (both come from THIS Repetition's iteration enumeration)
/// instead of storing a mirror on every candidate. The family-redundancy
/// filters the policy runs (`extension_dominates` with the
/// `WhenCurrentIsExtended` guard, bidirectionally, then
/// `boundary_repair_outranks_no_edit_iteration`) only ever compare
/// candidates from one producer, so there is no cross-combinator origin
/// axis to carry, and neither is a replay-equivalence dominance — they
/// remove redundancy inside the same-anchor extension family.
void consider_iteration_attempt(IterationSelectionResult &selection,
                                const EditableRecoveryCandidate &candidate,
                                const IterationReplayPlan &plan) {
  if (consider_editable_candidate(
          candidate, selection.bestCandidate,
          FamilyRedundancyPolicy::IterationExtensionFamily) ==
      SelectionVerdict::Take) {
    selection.bestCandidate = candidate;
    selection.winningPlan = plan;
  }
}

[[nodiscard]] bool
has_legal_iteration_retry_plan(const IterationPlanList &plans) noexcept {
  for (const auto &plan : plans) {
    if (plan.legal && plan.kind != IterationReplayKind::NoInsert) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] std::optional<std::size_t>
single_legal_iteration_retry_plan_index(const IterationPlanList &plans) noexcept {
  std::optional<std::size_t> singleIndex;
  for (std::size_t i = 0; i < plans.size(); ++i) {
    if (const auto &plan = plans[i];
        !plan.legal || plan.kind == IterationReplayKind::NoInsert) {
      continue;
    }
    if (singleIndex.has_value()) {
      return std::nullopt;
    }
    singleIndex = i;
  }
  return singleIndex;
}

[[nodiscard]] EditableRecoveryCandidate
capture_iteration_candidate(RecoveryContext &ctx,
                            const RecoveryContext::Checkpoint &iterationCheckpoint,
                            std::size_t recoveryEditCountBefore) {
  // Repetition ranks on the RAW iteration cursor (Invariant 2); the shared
  // constructor owns the rest of the field-population contract. The
  // checkpoint's recorded edit cost is the base the delta is measured from.
  return capture_editable_candidate(
      ctx, iterationCheckpoint.recoveryState.editBudget.editCost,
      recoveryEditCountBefore, KeyProgressConvention::RawIterationCursor);
}

[[nodiscard]] bool
iteration_retry_is_weak(const EditableRecoveryCandidate &candidate,
                        TextOffset parseStartOffset) noexcept {
  // Weak retries are boundary fabrications: the first edit happens at the
  // iteration entry and no visible continuation proves ownership. If the
  // first edit is later than the iteration entry, the element already
  // consumed source before repairing its tail (for example a missing close
  // delimiter at EOF), so it is a real started iteration and may outlive the
  // active window. The `editCount != 0` guard short-circuits before
  // `continues_after_first_edit` (which is only meaningful once an edit
  // exists).
  return candidate.matched && candidate.editCount != 0u &&
         !continues_after_first_edit(candidate) &&
         candidate.firstEditOffset <= parseStartOffset;
}

[[nodiscard]] bool
has_visible_remainder_after_current_cursor(const RecoveryContext &ctx) {
  return ctx.skip_without_builder(ctx.cursor()) < ctx.end;
}

bool replay_iteration(RecoveryContext &ctx, const RepetitionElementOps &ops,
                      const IterationReplayPlan &plan, const char *parseStart,
                      bool allowTrailingDeleteStop = false) {
  if (!plan.legal) {
    return false;
  }
  switch (plan.kind) {
  case IterationReplayKind::NoInsert:
    return ops.parseNoEdits(ops.self, ctx) && ctx.cursor() != parseStart;
  case IterationReplayKind::InsertRetry: {
    auto editGuard = ctx.withEditPermissions(plan.allowInsert, plan.allowDelete);
    (void)editGuard;
    ScopedBoolOverride destructiveContinuationGuard{
        ctx.allowDestructiveWindowContinuation,
        plan.allowDestructiveWindowContinuation ||
            ctx.allowDestructiveWindowContinuation};
    (void)destructiveContinuationGuard;
    return ops.withElementFollow(ops.self, ctx, [&]() {
      return ops.parseEditable(ops.self, ctx) && ctx.cursor() != parseStart;
    });
  }
  case IterationReplayKind::DeleteRetry: {
    auto editGuard = ctx.withEditPermissions(plan.allowInsert, plan.allowDelete);
    (void)editGuard;
    ScopedBoolOverride destructiveContinuationGuard{
        ctx.allowDestructiveWindowContinuation,
        plan.allowDestructiveWindowContinuation ||
            ctx.allowDestructiveWindowContinuation};
    (void)destructiveContinuationGuard;
    const auto parseWithRetryPermissions = [&ops, &ctx, parseStart]() {
      return ops.withElementFollow(ops.self, ctx, [&]() {
        return ops.parseEditable(ops.self, ctx) && ctx.cursor() != parseStart;
      });
    };
    {
      ProbeRestoreScope recoveryGuard{ctx};
      if (parseWithRetryPermissions()) {
        recoveryGuard.commit();
        return true;
      }
    }
    const bool canDeleteToEofAsTrailingGarbage =
        allowTrailingDeleteStop && ctx._followProbes.strict.fn == nullptr &&
        ctx._followProbes.recoverable.fn == nullptr;
    return recover_by_guarded_delete_retry(
        ctx,
        [&ops, &ctx,
         canDeleteToEofAsTrailingGarbage](const DeleteRetryVisitState &) {
          ProbeRestoreScope guard{ctx};
          return (canDeleteToEofAsTrailingGarbage && ctx.cursor() == ctx.end) ||
                 ops.fastProbe(ops.self, ctx) ||
                 ops.probeAtEntry(ops.self, ctx) ||
                 ops.probeLocallyRecoverable(ops.self, ctx);
        },
        [&ctx, &parseWithRetryPermissions,
         canDeleteToEofAsTrailingGarbage](const DeleteRetryVisitState &) {
          ProbeRestoreScope guard{ctx};
          const bool matched = parseWithRetryPermissions();
          const bool cleanedTrailingVisibleGarbage =
              canDeleteToEofAsTrailingGarbage && ctx.cursor() == ctx.end;
          if (matched) {
            guard.commit();
          }
          return matched || cleanedTrailingVisibleGarbage;
        },
        {.scan = {.allowOverflow = true},
         .stopOverflowAtStructuredVisibleSource = true});
  }
  }
  return false;
}

IterationObservation
observe_iteration(RecoveryContext &ctx, const RepetitionElementOps &ops,
                  const RecoveryContext::Checkpoint &iterationCheckpoint,
                  const char *parseStart, TextOffset parseStartOffset) {
  const auto visibleLeafCountBefore = ctx.failureHistorySize();
  const auto recoveryEditCountBefore = ctx.recoveryEditCount();
  EditableRecoveryCandidate noInsertCandidate{};
  if (ops.parseNoEdits(ops.self, ctx) && ctx.cursor() != parseStart) {
    noInsertCandidate = capture_iteration_candidate(ctx, iterationCheckpoint,
                                                    recoveryEditCountBefore);
  }
  IterationObservation observation{
      .noInsertCandidate = noInsertCandidate,
      .noInsertProbe =
          capture_recovery_probe_progress(ctx, visibleLeafCountBefore),
  };
  observation.noInsertProgressed =
      observation.noInsertProbe.committedProgressed(parseStartOffset);
  observation.startedWithoutEdits =
      !observation.noInsertProgressed &&
      ops.probeStartedWithoutEdits(ops.self, ctx);
  observation.iterationStarted =
      observation.noInsertProbe.exploredBeyond(parseStartOffset) ||
      observation.startedWithoutEdits;
  return observation;
}

/// Layer-1 of iteration-plan enumeration: observe the boundary facts.
///
/// Most `IterationSignals` fields are eager scalars; the probe-triggering
/// ones fire their (memoized) probes here via `&&` short-circuit, in the same
/// order the monolithic enumerator used:
///   - `speculative...` may fire `entryRecoveryConsumesVisible`;
///   - the two boundary-ownership predicates may fire
///     `probeRecoverableFollowConsumesVisibleHere` and the un-memoized
///     `evaluate_editable_recovery_candidate` boundary probe.
///
/// LOAD-BEARING: on the clean stop we return BEFORE the boundary-ownership
/// predicates, so their probes do not fire then (exactly as the old early
/// `return {}` did) and `boundaryOwnership` stays `Local`.
[[nodiscard]] IterationSignals observe_iteration_signals(
    RecoveryContext &ctx, const RepetitionElementOps &ops,
    bool skipBetweenIterations, const IterationObservation &observation,
    TextOffset parseStartOffset, std::size_t recoveryEditCountBefore,
    bool parseStartAtIterationEntryBoundary, bool firstStrictAccepts,
    pegium::utils::function_ref<bool()> entryRecoveryConsumesVisible) {
  IterationSignals sig;
  // The only boundary outcome the candidate enumeration consumes is the
  // clean stop: no local edit candidate, control returns to the parent.
  // It holds exactly when the body does not accept strictly at the cursor
  // (`firstStrict` is `noEditCandidate.matched`, reflecting FULL strict
  // acceptance — not the entry fast probe, which over-fires on a
  // multi-token body), the parent follow accepts strictly, and no
  // committed prefix forces the iteration to keep going.
  const auto &noEditCandidate = observation.noInsertCandidate;
  const bool followStrict = ctx.probeFollowAcceptsHere();
  const bool committedPrefixImposes = ctx.hasPendingCommittedRecoveryEdits();
  sig.stopCleanly =
      !noEditCandidate.matched && followStrict && !committedPrefixImposes;

  sig.noInsertCandidateLegal =
      observation.noInsertCandidate.matched && observation.noInsertProgressed;
  sig.firstZeroMinIteration = ops.minIsZero && !skipBetweenIterations;
  sig.retryContinuesStartedIteration =
      observation.noInsertProgressed || observation.iterationStarted;
  sig.noInsertProgressed = observation.noInsertProgressed;
  sig.iterationStarted = observation.iterationStarted;
  const auto hasEntryRecoveryRepairsBoundarySignal = [&]() {
    const auto probeCheckpoint = ctx.mark();
    ScopedBoolOverride leadingInsertProbeGuard{
        ctx.allowLeadingTerminalInsertScope,
        ctx.allowLeadingTerminalInsertScope || !ops.isOptional};
    (void)leadingInsertProbeGuard;
    const auto followedBody = [&]() {
      return ops.parseEditable(ops.self, ctx) &&
             ctx.cursorOffset() > parseStartOffset;
    };
    const auto candidate = evaluate_editable_recovery_candidate(
        ctx, probeCheckpoint, ctx.currentEditCost(), ctx.recoveryEditCount(),
        [&]() { return ops.withElementFollow(ops.self, ctx, followedBody); });
    return candidate.matched && candidate.editCount != 0u &&
           candidate.firstEditOffset == parseStartOffset &&
           candidate.postSkipCursorOffset > parseStartOffset;
  };
  sig.speculativeSyntheticSingleLeafRetryAfterRecoveredIteration =
      skipBetweenIterations && parseStartAtIterationEntryBoundary &&
      ctx.hasHadEdits() && !observation.startedWithoutEdits &&
      !observation.noInsertProgressed &&
      observation.noInsertProbe.exploredSingleVisibleLeafOrLess() &&
      !entryRecoveryConsumesVisible();
  // If the iteration started without prior edits, the enclosing Group's next
  // element already accepts the current cursor, and the NoInsert probe did
  // not progress, let iteration terminate cleanly instead of speculating
  // retry edits that would steal characters from the parent's follow. Edits
  // from outer recovery attempts must remain free to explore alternate
  // candidates, so gate only the ambient/clean entry point.
  const bool atLocalIterationBoundary =
      (skipBetweenIterations || ops.minIsZero) &&
      parseStartAtIterationEntryBoundary;
  const auto &shape = *ops.shape;
  const bool localRecoveryBeginsWithLexicalChainTail =
      !firstStrictAccepts && shape.hasInsertableTerminal &&
      shape.hasSyntheticTerminalThenLexicalTail;
  // Lazy + memoized (no ScopedBoolOverride here — do not add one); fires only
  // when a boundary-ownership predicate reaches it.
  std::optional<bool> parentFollowRecoverableConsumesVisibleCache;
  const auto parentFollowRecoverableConsumesVisible = [&]() {
    if (!parentFollowRecoverableConsumesVisibleCache.has_value()) {
      parentFollowRecoverableConsumesVisibleCache =
          ctx.probeRecoverableFollowConsumesVisibleHere();
    }
    return *parentFollowRecoverableConsumesVisibleCache;
  };
  if (sig.stopCleanly) {
    return sig;
  }
  // A broad repeated element can consume one visible leaf and then fabricate
  // the rest of its shape, hiding a recoverable parent boundary. This remains
  // unsafe after earlier edits; a recoverable parent follow may only insert a
  // delimiter here, with visible consumption happening in the parent's next
  // site.
  // A nullable repetition boundary may see an element-shaped prefix that
  // fails before committing progress. If the parent can recover at the same
  // cursor and then consume visible input, the prefix is weaker evidence than
  // the parent boundary: local retry would reinterpret the next sibling as an
  // iteration item.
  const bool recoverableParentBoundaryOwnsWeakStartedRetry =
      atLocalIterationBoundary && !shape.hasRequiredLiteralAnchor &&
      observation.startedWithoutEdits && !observation.noInsertProgressed &&
      !followStrict && !committedPrefixImposes &&
      parentFollowRecoverableConsumesVisible() &&
      !hasEntryRecoveryRepairsBoundarySignal();
  // Same ownership rule for a fresh first iteration whose local recovery
  // begins with a lexical chain tail: the parent follow's visible
  // continuation is the stronger structural owner.
  const bool recoverableParentBoundaryOwnsFreshLexicalChainTail =
      atLocalIterationBoundary && recoveryEditCountBefore == 0u &&
      !ctx.hasHadEdits() && !observation.startedWithoutEdits &&
      !observation.noInsertProgressed &&
      localRecoveryBeginsWithLexicalChainTail && !followStrict &&
      !committedPrefixImposes && parentFollowRecoverableConsumesVisible();
  // Who owns the next visible token at this boundary. Reads the two
  // already-evaluated predicates above — no probe re-fires.
  sig.boundaryOwnership = (recoverableParentBoundaryOwnsWeakStartedRetry ||
                           recoverableParentBoundaryOwnsFreshLexicalChainTail)
                              ? BoundaryOwnership::Parent
                              : BoundaryOwnership::Local;
  return sig;
}

/// Layer-2 of iteration-plan enumeration: turn the observed signals into the
/// four ordered replay plans, each marked `.legal`;
/// `consider_iteration_attempt` does the central ranking (plan ORDER encodes
/// the considered precedence, mirroring `OrderedChoice`).
///
/// VERDICT SLOTS (precedence order):
///   [0] NoInsert                        — body matched strictly AND progressed.
///   [1] InsertRetry                     — synthesize the leading element, retry.
///   [2] DeleteRetry                     — delete noise, retry the started item.
///   [3] boundary-preserving InsertRetry — copy of [1] with allowDelete=false,
///       active only when [1].legal && [1].allowDelete && entry boundary.
///
/// LEGALITY IS NOT AN `iff` PER PLAN — it is an ORDERED patch sequence whose
/// final value depends on patch order (read top-to-bottom):
///   deleteRetryLegal: false
///     -> if(!noInsertCandidateLegal && !speculative)
///          iterationStarted || (skip && !hadEdits && probe_locally_recoverable)
///     -> if recoverableFirstZeroMinAfterPriorRecovery: true
///     -> if ownership==Parent: forced false.
///   insertRetryLegal: (noInsertCandidateLegal && allowInsert)
///     -> if false: = deleteRetryLegal
///     -> if speculative: forced false
///     -> three re-enable patches (firstZeroMin entry-signal; locally-
///        recoverable-after-skip; in-window entry-consumes-visible), each
///        guarded by !insertRetryLegal (so their probes fire only when needed)
///     -> if ownership==Parent: forced false.
///
/// LOAD-BEARING INVARIANT (lets the two-point speculative application collapse
/// to one force-false): firstZeroMinIteration requires !skipBetweenIterations
/// while `speculative` requires skipBetweenIterations -> mutually exclusive.
///
/// LAZY PROBES (`entryRecoveryConsumesVisible`, memoized via the orchestrator;
/// `probe_locally_recoverable`, `has_entry_recovery_signal`,
/// `is_locally_recoverable_after_iteration_skip`) fire ONLY on the patch that
/// needs them, behind their `&&` short-circuit — they MUST stay lazy.
[[nodiscard]] IterationPlanList decide_iteration_plans(
    RecoveryContext &ctx, const RepetitionElementOps &ops,
    const IterationSignals &sig, bool skipBetweenIterations,
    bool parseStartAtIterationEntryBoundary,
    pegium::utils::function_ref<bool()> entryRecoveryConsumesVisible) {
  bool deleteRetryLegal = false;
  if (!sig.noInsertCandidateLegal &&
      !sig.speculativeSyntheticSingleLeafRetryAfterRecoveredIteration) {
    deleteRetryLegal = sig.iterationStarted ||
                       (skipBetweenIterations && !ctx.hasHadEdits() &&
                        ops.probeLocallyRecoverable(ops.self, ctx));
  }
  // A zero-min repetition is skippable, but not allowed to discard visible
  // source that clearly belongs to its first item after the parent already
  // opened the surrounding structure by recovery. Keep the item repair
  // admissible only when the entry probe proves visible consumption; pure
  // synthetic first items still stop cleanly.
  const bool recoverableFirstZeroMinAfterPriorRecovery =
      sig.firstZeroMinIteration && ctx.hasHadEdits() &&
      !sig.noInsertCandidateLegal && !sig.noInsertProgressed &&
      entryRecoveryConsumesVisible();
  if (!deleteRetryLegal && recoverableFirstZeroMinAfterPriorRecovery) {
    deleteRetryLegal = true;
  }
  bool insertRetryLegal =
      sig.noInsertCandidateLegal && ctx.editPolicy.allowInsert;
  if (!insertRetryLegal) {
    insertRetryLegal = deleteRetryLegal;
  }
  if (sig.speculativeSyntheticSingleLeafRetryAfterRecoveredIteration) {
    insertRetryLegal = false;
  }
  if (!insertRetryLegal && sig.firstZeroMinIteration &&
      !sig.noInsertCandidateLegal && !ctx.hasHadEdits()) {
    insertRetryLegal = repetition_has_entry_recovery_signal(ctx, ops);
  }
  if (!insertRetryLegal && !sig.noInsertCandidateLegal &&
      ctx.editPolicy.allowInsert && !ctx.hasHadEdits()) {
    insertRetryLegal =
        repetition_is_locally_recoverable_after_iteration_skip(ctx, ops);
  }
  if (!insertRetryLegal && skipBetweenIterations &&
      parseStartAtIterationEntryBoundary && ctx.hasHadEdits() &&
      !sig.noInsertProgressed && entryRecoveryConsumesVisible()) {
    insertRetryLegal = true;
  }
  if (sig.boundaryOwnership == BoundaryOwnership::Parent) {
    // The repeated element only proved a weak or synthetic local start while
    // the parent's follow can recover at the same boundary. Keep ownership
    // with the parent; later local fallback paths must not reopen retry
    // plans.
    deleteRetryLegal = false;
    insertRetryLegal = false;
  }
  // Lookahead gate for insert-into-option recovery.
  //
  // For a strictly optional repetition (`option(X)` = `Repetition<0,1>`),
  // the first iteration starts with no committed edits. If no clean
  // (no-insert) iteration is legal at this point, the parser has two
  // choices: skip the option (its `min=0` makes that legitimate), or
  // synthesize the iteration by inserting its leading element. The
  // insert opens a speculative path that frequently cascades into a
  // multi-edit fabrication (e.g. inserting `(` so a bare ID becomes a
  // `FunctionCall` with synthetic args, then needing more edits to
  // close the parens and patch the body). When the input has not even
  // started the option's body, the cleanest recovery is almost always
  // to skip — the surrounding rule will resync via its own recovery.
  //
  // Plan order encodes the considered precedence directly (the central
  // ranking still breaks ties): NoInsert, then InsertRetry, then
  // DeleteRetry, then the boundary-preserving insert. This reproduces the
  // order the former pre-evaluation pass imposed (insert before delete when
  // both are legal) without a separate scan.
  const bool destructiveWindowContinuation =
      ctx.hasHadEdits() && parseStartAtIterationEntryBoundary &&
      sig.retryContinuesStartedIteration;
  IterationPlanList plans{
      IterationReplayPlan{
          .kind = IterationReplayKind::NoInsert,
          .legal = sig.noInsertCandidateLegal,
      },
      IterationReplayPlan{.kind = IterationReplayKind::InsertRetry,
                          .legal = insertRetryLegal,
                          .allowInsert = true,
                          .allowDelete = sig.retryContinuesStartedIteration &&
                                         ctx.editPolicy.allowDelete,
                          .allowDestructiveWindowContinuation =
                              destructiveWindowContinuation},
      IterationReplayPlan{.kind = IterationReplayKind::DeleteRetry,
                          .legal = deleteRetryLegal,
                          .allowDelete = ctx.editPolicy.allowDelete,
                          .allowDestructiveWindowContinuation =
                              destructiveWindowContinuation},
      IterationReplayPlan{
          .kind = IterationReplayKind::InsertRetry,
      }};
  auto &insertRetryPlan = plans[1];
  auto &boundaryPreservingInsertRetryPlan = plans[3];
  if (insertRetryPlan.legal && insertRetryPlan.allowDelete &&
      parseStartAtIterationEntryBoundary) {
    boundaryPreservingInsertRetryPlan = insertRetryPlan;
    boundaryPreservingInsertRetryPlan.allowDelete = false;
  }
  // The clean-stop boundary (parent strict follow accepts and the
  // committed prefix does not impose) already returned no plans above.
  // The remaining boundary cases let the per-plan `legal` flags drive
  // the choice, except that `Resync` is gated only when the iteration is
  // OUTSIDE an active recovery window: inside an active window, the
  // dispatch's entry-recovery-signal fallback heuristics produce
  // legitimate retries a coarse boundary signal cannot distinguish from
  // speculative ones.
  return plans;
}

/// Enumerates the iteration replay plans in two layers: observe the boundary
/// facts (`observe_iteration_signals`), then decide the ordered legality
/// verdicts (`decide_iteration_plans`). The clean stop short-circuits to no
/// plans between the two.
///
/// The `entryRecoveryConsumesVisible` probe is owned HERE so its memoized
/// result is shared by both layers and fires at most once, only when a
/// verdict actually needs it. The probe deliberately leaks `_maxCursor` (no
/// ProbeRestoreScope) — the caller reads `ctx.maxCursor()` for furthest-error
/// offsets — so WHEN it fires is observable: it must never become eager.
[[nodiscard]] IterationPlanList enumerate_iteration_plans(
    RecoveryContext &ctx, const RepetitionElementOps &ops,
    bool skipBetweenIterations, const IterationObservation &observation,
    TextOffset parseStartOffset, std::size_t recoveryEditCountBefore,
    bool parseStartAtIterationEntryBoundary, bool firstStrictAccepts) {
  std::optional<bool> entryRecoveryConsumesVisibleCache;
  const auto entryRecoveryConsumesVisible = [&]() {
    if (!entryRecoveryConsumesVisibleCache.has_value()) {
      ScopedBoolOverride leadingInsertProbeGuard{
          ctx.allowLeadingTerminalInsertScope,
          ctx.allowLeadingTerminalInsertScope || !ops.isOptional};
      (void)leadingInsertProbeGuard;
      entryRecoveryConsumesVisibleCache =
          ops.probeAtEntryConsumesVisible(ops.self, ctx);
    }
    return *entryRecoveryConsumesVisibleCache;
  };
  const IterationSignals sig = observe_iteration_signals(
      ctx, ops, skipBetweenIterations, observation, parseStartOffset,
      recoveryEditCountBefore, parseStartAtIterationEntryBoundary,
      firstStrictAccepts, entryRecoveryConsumesVisible);
  if (sig.stopCleanly) {
    return {};
  }
  return decide_iteration_plans(ctx, ops, sig, skipBetweenIterations,
                                parseStartAtIterationEntryBoundary,
                                entryRecoveryConsumesVisible);
}

[[nodiscard]] EditableRecoveryCandidate evaluate_retry_iteration_attempt(
    RecoveryContext &ctx, const RepetitionElementOps &ops,
    const IterationReplayPlan &plan,
    const RecoveryContext::Checkpoint &iterationCheckpoint,
    const RecoveryContext::Checkpoint &parseCheckpoint, const char *parseStart,
    const char *parseStartFurthestExploredCursor,
    const char *&furthestExploredCursor, std::size_t recoveryEditCountBefore,
    bool skipBetweenIterations) {
  EditableRecoveryCandidate candidate{};
  const bool valid = replay_iteration(ctx, ops, plan, parseStart,
                                      skipBetweenIterations || ops.minIsZero);
  if (ctx.maxCursor() > furthestExploredCursor) {
    furthestExploredCursor = ctx.maxCursor();
  }
  if (valid) {
    candidate = capture_iteration_candidate(ctx, iterationCheckpoint,
                                            recoveryEditCountBefore);
  }
  ctx.rewind(parseCheckpoint);
  ctx.restoreMaxCursor(parseStartFurthestExploredCursor);
  return candidate;
}

IterationSelectionResult select_iteration_attempt(
    RecoveryContext &ctx, const RepetitionElementOps &ops,
    const IterationPlanList &plans, const IterationObservation &observation,
    const RecoveryContext::Checkpoint &iterationCheckpoint,
    const RecoveryContext::Checkpoint &parseCheckpoint, const char *parseStart,
    const char *parseStartFurthestExploredCursor,
    const char *&furthestExploredCursor, std::size_t recoveryEditCountBefore,
    bool skipBetweenIterations) {
  IterationSelectionResult selection;
  // Plans are enumerated in considered-precedence order, so a single pass
  // evaluates each legal plan once; the central ranking breaks ties.
  for (std::size_t i = 0; i < plans.size(); ++i) {
    const auto &plan = plans[i];
    if (!plan.legal) {
      continue;
    }
    if (plan.kind == IterationReplayKind::NoInsert) {
      selection.bestCandidate = observation.noInsertCandidate;
      selection.winningPlan = plan;
      continue;
    }
    const auto retryCandidate = evaluate_retry_iteration_attempt(
        ctx, ops, plan, iterationCheckpoint, parseCheckpoint, parseStart,
        parseStartFurthestExploredCursor, furthestExploredCursor,
        recoveryEditCountBefore, skipBetweenIterations);
    consider_iteration_attempt(selection, retryCandidate, plan);
  }

  return selection;
}

/// Last-resort panic-mode resync skip. Pass 1 deletes noise until the
/// iteration element itself re-parses cleanly (no destructive edit) from a
/// fresh start. When `allowParentFollowFallback` is set, pass 2 (after
/// rewinding to the entry) instead deletes until the parent follow accepts
/// (or an implicit EOF follow). Both passes share one entry guard, one
/// extended-delete-scan budget (the budget overrides config fields, not
/// checkpoint state, so the inter-pass rewind leaves it enabled), and one
/// restore epilogue.
[[nodiscard]] bool try_resync_skip(RecoveryContext &ctx,
                                   const RepetitionElementOps &ops,
                                   const char *parseStart,
                                   TextOffset parseStartOffset,
                                   bool allowParentFollowFallback) {
  if (!ctx.editPolicy.allowDelete || ctx.maxResyncSkipCodepoints == 0u ||
      ctx.cursor() != parseStart || ctx.cursor() >= ctx.end ||
      ctx.probeFollowAcceptsHere()) {
    return false;
  }
  // Do not reinterpret bytes inside a committed-prefix replay: the driver
  // already fixed the edit script for that region at an earlier site.
  if (ctx.hasPendingCommittedRecoveryEdits() &&
      parseStartOffset < ctx.committedScript.resumeFloor) {
    return false;
  }
  const auto recoveryCheckpoint = ctx.mark();
  const char *const savedFurthestExploredCursor = ctx.maxCursor();
  ScopedBoolOverride destructiveContinuationGuard{
      ctx.allowDestructiveWindowContinuation,
      ctx.allowDestructiveWindowContinuation || ctx.hasHadEdits()};
  (void)destructiveContinuationGuard;
  ExtendedDeleteScanBudgetScope<RecoveryContext> budgetScope{ctx};
  if (!budgetScope.tryEnable()) {
    return false;
  }
  const std::uint32_t codepointBudget = ctx.maxResyncSkipCodepoints;

  std::uint32_t deleted = 0u;
  while (deleted < codepointBudget && ctx.cursor() < ctx.end) {
    if (ctx.probeFollowAcceptsHere()) {
      break;
    }
    if (!ctx.deleteOneCodepoint()) {
      break;
    }
    ++deleted;
    // Cheap probe first: fast_probe short-circuits without exploring
    // recovery state. `probe_recoverable_at_entry` is the supercategory.
    // Mirrors the order in `probeRecoverableAtEntryConsumesVisible`.
    if (!ops.fastProbe(ops.self, ctx) && !ops.probeAtEntry(ops.self, ctx)) {
      continue;
    }
    const auto parseCheckpoint = ctx.mark();
    const char *const parseCursor = ctx.cursor();
    const auto targetRecoveryEditCount = ctx.recoveryEditCount();
    if ((ops.parseNoEdits(ops.self, ctx) ||
         ops.withElementFollow(
             ops.self, ctx,
             [&]() { return ops.parseEditable(ops.self, ctx); })) &&
        ctx.cursor() != parseCursor) {
      if (const auto editSummary = summarize_edits_since(
              ctx.recoveryEditsView(), targetRecoveryEditCount);
          editSummary.hasDestructiveEdit) {
        ctx.rewind(parseCheckpoint);
        continue;
      }
      budgetScope.commitOverflowEdits();
      return true;
    }
    ctx.rewind(parseCheckpoint);
  }

  if (allowParentFollowFallback) {
    // Reset to the iteration entry so the parent-follow scan starts clean.
    ctx.rewind(recoveryCheckpoint);
    deleted = 0u;
    while (deleted < codepointBudget && ctx.cursor() < ctx.end) {
      if (!ctx.deleteOneCodepoint()) {
        break;
      }
      ++deleted;
      const bool reachedImplicitEofFollow =
          ctx.cursor() == ctx.end && ctx._followProbes.strict.fn == nullptr &&
          ctx._followProbes.recoverable.fn == nullptr;
      if (ctx.probeFollowAcceptsHere() || reachedImplicitEofFollow) {
        budgetScope.commitOverflowEdits();
        return true;
      }
    }
  }

  ctx.rewind(recoveryCheckpoint);
  ctx.bumpMaxCursor(savedFurthestExploredCursor);
  return false;
}

} // namespace

bool repetition_has_entry_recovery_signal(RecoveryContext &ctx,
                                          const RepetitionElementOps &ops) {
  if (!has_visible_remainder_after_current_cursor(ctx)) {
    return false;
  }
  return ops.probeAtEntry(ops.self, ctx);
}

bool repetition_is_locally_recoverable_after_iteration_skip(
    RecoveryContext &ctx, const RepetitionElementOps &ops) {
  ProbeRestoreScope guard{ctx};
  ctx.skip();
  return ops.probeLocallyRecoverable(ops.self, ctx);
}

bool repetition_probe_resync_entry_consumes_visible(
    RecoveryContext &ctx, const RepetitionElementOps &ops) {
  const auto maxDeletes = ops.isOptional
                              ? ctx.editPolicy.maxConsecutiveCodepointDeletes
                              : ctx.maxResyncSkipCodepoints;
  if (!ctx.editPolicy.allowDelete || maxDeletes == 0u ||
      ctx.cursor() >= ctx.end) {
    return false;
  }
  ScopedBoolOverride destructiveContinuationGuard{
      ctx.allowDestructiveWindowContinuation,
      ctx.allowDestructiveWindowContinuation || ctx.hasHadEdits()};
  (void)destructiveContinuationGuard;
  ProbeRestoreScope guard{ctx};
  bool consumesVisible = false;
  (void)visit_guarded_delete_scan_positions(
      ctx, [&ctx]() { return !ctx.probeFollowAcceptsHere(); },
      [&ops, &ctx, &consumesVisible](const DeleteScanVisitState &) {
        using enum DeleteScanVisitResult;
        if (ctx.probeFollowAcceptsHere()) {
          return Stop;
        }
        if (ops.fastProbe(ops.self, ctx) ||
            ops.probeAtEntryConsumesVisible(ops.self, ctx)) {
          consumesVisible = true;
          return Accept;
        }
        return Continue;
      },
      {.scan = {.maxDeletes = maxDeletes, .allowOverflow = !ops.isOptional}});
  return consumesVisible;
}

bool repetition_blocked_frontier_stops_cleanly_after_window_progress(
    const RecoveryContext &ctx) noexcept {
  return ctx.frontierBlocked() && ctx.hasHadEdits() &&
         ctx.hasPendingRecoveryWindows() &&
         ctx.maxCursorOffset() >= ctx.pendingRecoveryWindowMaxCursorOffset();
}

bool repetition_finalize_optional_strict_success(
    RecoveryContext &ctx, const RepetitionElementOps &ops,
    const RecoveryContext::Checkpoint &checkpoint,
    const char *savedFurthestExploredCursor) {
  if (!has_visible_remainder_after_current_cursor(ctx)) {
    ctx.bumpMaxCursor(savedFurthestExploredCursor);
    return true;
  }

  const auto strictCursorOffset = ctx.cursorOffset();
  const auto strictExploredOffset =
      std::max(strictCursorOffset, ctx.maxCursorOffset());
  const auto strictPostSkipOffset = post_skip_cursor_offset(ctx);
  const bool strictFrontierIsRecoveryRelevant =
      ctx.canEditAtOffset(strictPostSkipOffset) ||
      (ctx.hasPendingRecoveryWindows() &&
       strictPostSkipOffset >= ctx.pendingRecoveryWindowBeginOffset() &&
       strictPostSkipOffset <= ctx.pendingRecoveryWindowMaxCursorOffset());
  if (!strictFrontierIsRecoveryRelevant) {
    ctx.bumpMaxCursor(savedFurthestExploredCursor);
    return true;
  }

  ctx.rewind(checkpoint);
  ctx.restoreMaxCursor(savedFurthestExploredCursor);
  const auto baseEditCost = ctx.currentEditCost();
  const auto baseRecoveryEditCount = ctx.recoveryEditCount();
  if (baseRecoveryEditCount != 0u &&
      strictExploredOffset == strictCursorOffset) {
    const bool matchedStrictly = ops.parseNoEdits(ops.self, ctx);
    ctx.bumpMaxCursor(savedFurthestExploredCursor);
    return matchedStrictly;
  }
  // A strictly matched optional already established a local boundary. Only
  // let the editable replay compete when it repairs the continuation inside
  // the local frontier already explored by the strict parse, instead of
  // reopening the already matched prefix or speculating deeper into the
  // remainder.
  const auto editableCandidate = evaluate_editable_recovery_candidate(
      ctx, checkpoint, baseEditCost, baseRecoveryEditCount,
      [&]() { return ops.parseEditable(ops.self, ctx); });
  const bool preferEditableMatch =
      editableCandidate.matched && editableCandidate.editCount > 0u &&
      editableCandidate.firstEditOffset >= strictCursorOffset &&
      editableCandidate.firstEditOffset <= strictExploredOffset &&
      continues_after_first_edit(editableCandidate) &&
      editableCandidate.postSkipCursorOffset > strictPostSkipOffset;
  if (preferEditableMatch) {
    return ops.parseEditable(ops.self, ctx);
  }

  const bool matchedStrictly = ops.parseNoEdits(ops.self, ctx);
  ctx.bumpMaxCursor(savedFurthestExploredCursor);
  return matchedStrictly;
}

bool repetition_try_recovery_iteration(RecoveryContext &ctx,
                                       const RepetitionElementOps &ops,
                                       bool skipBetweenIterations) {
  const auto iterationCheckpoint = ctx.mark();
  const auto recoveryEditCountBefore = ctx.recoveryEditCount();
  if (skipBetweenIterations) {
    ctx.skip();
  }
  const auto parseCheckpoint = ctx.mark();
  const char *const parseStart = ctx.cursor();
  const char *const parseStartFurthestExploredCursor = ctx.maxCursor();
  const TextOffset parseStartOffset = ctx.cursorOffset();
  const bool parseStartAtIterationEntryBoundary =
      post_skip_cursor_offset(ctx) == parseStartOffset;
  // Capture the cheap element strict probe at the parseStart cursor
  // (where the iteration would begin a fresh attempt). The recoverable
  // entry probe is deliberately lazy inside `enumerate_iteration_plans`:
  // it can parse deeply, while the boundary table needs it only when no
  // strict element/follow/started-iteration fact already decides the case.
  bool firstStrictAccepts = false;
  {
    ProbeRestoreScope guard{ctx};
    firstStrictAccepts = ops.fastProbe(ops.self, ctx);
  }
  const auto observation = observe_iteration(ctx, ops, iterationCheckpoint,
                                             parseStart, parseStartOffset);
  const auto iterationPlans = enumerate_iteration_plans(
      ctx, ops, skipBetweenIterations, observation, parseStartOffset,
      recoveryEditCountBefore, parseStartAtIterationEntryBoundary,
      firstStrictAccepts);
  const bool hasLegalRetryPlan = has_legal_iteration_retry_plan(iterationPlans);
  const char *furthestExploredCursor = ctx.maxCursor();
  // `furthestExploredCursor` is mutated by reference inside
  // `select_iteration_attempt`, so the bail lambda captures by reference to
  // observe the latest value at each call site.
  const auto bailToIterationEntry = [&] {
    ctx.rewind(iterationCheckpoint);
    ctx.bumpMaxCursor(furthestExploredCursor);
    return false;
  };
  if (!iterationPlans.front().legal) {
    const auto singleRetryIndex =
        single_legal_iteration_retry_plan_index(iterationPlans);
    if (singleRetryIndex.has_value()) {
      ctx.rewind(parseCheckpoint);
      ctx.restoreMaxCursor(parseStartFurthestExploredCursor);
      const auto &singlePlan = iterationPlans[*singleRetryIndex];
      if (replay_iteration(ctx, ops, singlePlan, parseStart,
                           skipBetweenIterations || ops.minIsZero)) {
        return true;
      }
    }
  }
  ctx.rewind(parseCheckpoint);
  ctx.restoreMaxCursor(parseStartFurthestExploredCursor);
  // No legal local attempt: the no-edit branch did not progress and no
  // retry plan is admissible, so select_iteration_attempt would evaluate
  // nothing and return an unmatched best. Skip it and fall through to the
  // shared resync fallback below. The insertable-terminal deferral applies
  // ONLY on this path — defer to the parent follow rather than resync-skip;
  // it must not leak into the matched-but-failed-selection path.
  const bool noLegalLocalAttempt =
      !observation.noInsertProgressed && !hasLegalRetryPlan;
  if (noLegalLocalAttempt && ops.shape->hasInsertableTerminal &&
      (ctx.probeFollowAcceptsHere() || ctx.probeRecoverableFollowHere())) {
    return bailToIterationEntry();
  }
  const IterationSelectionResult selection =
      noLegalLocalAttempt
          ? IterationSelectionResult{}
          : select_iteration_attempt(
                ctx, ops, iterationPlans, observation, iterationCheckpoint,
                parseCheckpoint, parseStart, parseStartFurthestExploredCursor,
                furthestExploredCursor, recoveryEditCountBefore,
                skipBetweenIterations);
  if (!selection.bestCandidate.matched) {
    if (try_resync_skip(
            ctx, ops, parseStart, parseStartOffset,
            /*allowParentFollowFallback=*/skipBetweenIterations ||
                ops.minIsZero)) {
      return true;
    }
    return bailToIterationEntry();
  }
  const bool weakRetryEscapesActiveWindow =
      skipBetweenIterations && recoveryEditCountBefore > 0u &&
      iteration_retry_is_weak(selection.bestCandidate, parseStartOffset);
  if (weakRetryEscapesActiveWindow) {
    return bailToIterationEntry();
  }
  // Rebuild only the winning branch from the shared parse checkpoint instead
  // of keeping success checkpoints alive across competing recovery parses.
  ctx.rewind(parseCheckpoint);
  ctx.restoreMaxCursor(parseStartFurthestExploredCursor);
  const bool replayed =
      replay_iteration(ctx, ops, selection.winningPlan, parseStart,
                       skipBetweenIterations || ops.minIsZero);
  if (!replayed) {
    return bailToIterationEntry();
  }
  return replayed;
}

bool repetition_parse_zero_min_recovery(RecoveryContext &ctx,
                                        const RepetitionElementOps &ops,
                                        std::size_t maxIterationCount) {
  const auto checkpoint = ctx.mark();
  const char *const savedFurthestExploredCursor = ctx.maxCursor();
  if (!repetition_try_recovery_iteration(ctx, ops,
                                         /*skipBetweenIterations=*/false)) {
    if (ctx.frontierBlocked()) {
      ctx.rewind(checkpoint);
      ctx.bumpMaxCursor(savedFurthestExploredCursor);
      return false;
    }
    ctx.rewind(checkpoint);
    ctx.bumpMaxCursor(savedFurthestExploredCursor);
    // Zero-min repetitions may stop cleanly when a speculative entry repair
    // does not turn into a real first iteration.
    PEGIUM_STEP_TRACE_INC(StepCounter::RepetitionFastAttempts);
    const bool startedWithoutEdits =
        ops.probeStartedWithoutEdits(ops.self, ctx);
    PEGIUM_STEP_TRACE_INC(startedWithoutEdits
                              ? StepCounter::RepetitionFastSuccess
                              : StepCounter::RepetitionFastFailures);
    return !startedWithoutEdits;
  }
  for (std::size_t matchedCount = 1u; matchedCount < maxIterationCount;
       ++matchedCount) {
    if (!repetition_try_recovery_iteration(ctx, ops,
                                           /*skipBetweenIterations=*/true)) {
      break;
    }
  }
  if (ctx.frontierBlocked()) {
    if (repetition_blocked_frontier_stops_cleanly_after_window_progress(ctx)) {
      ctx.clearFrontierBlock();
      return true;
    }
    return false;
  }
  return true;
}

} // namespace pegium::parser::detail
