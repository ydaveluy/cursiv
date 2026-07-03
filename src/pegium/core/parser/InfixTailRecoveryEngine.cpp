#include <pegium/core/parser/InfixTailRecoveryEngine.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <limits>
#include <optional>

#include <pegium/core/parser/ContextShared.hpp>
#include <pegium/core/parser/RecoveryCandidate.hpp>
#include <pegium/core/parser/RecoveryCost.hpp>
#include <pegium/core/parser/RecoveryUtils.hpp>
#include <pegium/core/utils/TextUtils.hpp>

namespace pegium::parser::detail {

namespace {

struct EditableTailCandidate {
  InfixCandidateFamily kind = InfixCandidateFamily::DeleteOperatorNoise;
  std::uint32_t rhsPrefixNoiseDeleteCount = 0;
  bool rhsPrimaryObservedWithoutEdits = false;
  bool legal = false;
};

struct EditableTailObservation {
  TextOffset rhsStartOffset = 0;
  std::uint32_t rhsPrefixNoiseDeleteCount = 0;
  // Populated before the observation's `ctx.rewind()` so the
  // caller can still project the speculative match onto a
  // `RecoveryKey` after the speculative edits have been rolled
  // back.
  std::uint32_t immediateEditCount = 0;
  std::uint32_t immediateEditCost = 0;
  TextOffset firstImmediateEditOffset = std::numeric_limits<TextOffset>::max();
  TextOffset postMatchCursorOffset = 0;
  // Real codepoint-delete count captured by the observable
  // `observe_stray_operator_run_delete_count` scan from the
  // pre-operator-match cursor. Used to project a real
  // `RecoveryKey` for the `DeleteOperatorNoise` candidate.
  // Empty when the scan would not admit a recovery from this
  // cursor.
  std::optional<std::uint32_t> strayOperatorRunDeleteCount;
  TextOffset strayOperatorRunFirstEditOffset = 0;
  bool exposedPrimary = true;
  bool matchedPrimary = false;
  bool advancedPrimary = false;
  bool rhsPrimaryObservedWithoutEdits = false;
  bool multipleImmediateEditsAtStart = false;
};

/// Shared bounded delete-scan over a stray operator run: skip operator
/// noise until the operator probe re-accepts, unless a recoverable
/// primary hides behind a local trivia gap. The caller owns the
/// skip-after-delete policy (and, for observation, the rewind guard) and
/// supplies the on-match sink (`recover_by_guarded_delete_scan`
/// dispatches the 1- vs 2-argument sink).
template <typename OnMatch>
[[nodiscard]] bool scan_stray_operator_run(RecoveryContext &ctx,
                                           const InfixTailOps &ops,
                                           std::int32_t minPrecedence,
                                           OnMatch &&onMatch) {
  const auto hidesRecoverablePrimaryBehindLocalGap = [&]() {
    ProbeRestoreScope guard{ctx};
    const char *const gapStart = ctx.cursor();
    const char *const gapEnd = ctx.skip_without_builder(gapStart);
    ctx.skip();
    // A "real" separator spans more than a single trivia *codepoint*.
    // Counting bytes (`gapEnd > gapStart + 1`) wrongly treats one
    // multi-byte whitespace codepoint (U+00A0, U+3000, …) as a gap; count
    // codepoints so the test is independent of UTF-8 width. Bounded to two
    // decodes — we only need "> 1 codepoint" — to stay cheap on this path.
    std::size_t gapCodepoints = 0u;
    for (const char *p = gapStart; p < gapEnd && gapCodepoints < 2u;) {
      const auto length = pegium::utils::utf8_codepoint_length(*p);
      p += length == 0u ? 1u : length;
      ++gapCodepoints;
    }
    return gapCodepoints > 1u && ctx.cursor() < ctx.end &&
           (ops.fastProbePrimary(ops.model, ctx) ||
            ops.probeLocallyRecoverablePrimary(ops.model, ctx));
  };
  return recover_by_guarded_delete_scan(
      ctx,
      [&]() {
        return ops.operatorFastProbe(ops.model, ctx, minPrecedence) ||
               !hidesRecoverablePrimaryBehindLocalGap();
      },
      [&](const char *scanCursor) -> const char * {
        if (scanCursor == ctx.end ||
            ops.operatorFastProbe(ops.model, ctx, minPrecedence) ||
            hidesRecoverablePrimaryBehindLocalGap()) {
          return nullptr;
        }
        return scanCursor;
      },
      std::forward<OnMatch>(onMatch));
}

/// Observable variant of `recover_stray_operator_run`: runs the same
/// delete-scan logic against the cursor without committing any mutation
/// (cursor, skipAfterDelete, edits all rewound), and returns the number
/// of codepoint deletes the scan would produce on commit. `std::nullopt`
/// means the scan would not admit a `DeleteOperatorNoise` recovery from
/// the current cursor (operator probe fails or the scan runs past its
/// budget without matching). Used to project a real `RecoveryKey` for the
/// `DeleteOperatorNoise` candidate at selection time.
[[nodiscard]] std::optional<std::uint32_t>
observe_stray_operator_run_delete_count(RecoveryContext &ctx,
                                        const InfixTailOps &ops,
                                        std::int32_t minPrecedence) {
  if (!ops.operatorFastProbe(ops.model, ctx, minPrecedence)) {
    return std::nullopt;
  }
  std::optional<std::uint32_t> capturedDeleteCount;
  {
    ProbeRestoreScope guard{ctx};
    // Disable `skipAfterDelete` for the duration of the stray-operator-run
    // probe. Without this flip, the bounded delete scan would absorb a
    // hidden separator that crosses a logical boundary and commit to a
    // wrong run length. The mutation is observed by
    // `RecoveryPolicyFingerprint` so the recovery cache cannot collapse a
    // hit across the flipped state.
    ScopedBoolOverride skipPolicy{ctx.skipAfterDelete, false};
    (void)scan_stray_operator_run(
        ctx, ops, minPrecedence, [&](const char *, std::uint32_t deleteCount) {
          capturedDeleteCount = deleteCount;
        });
  }
  return capturedDeleteCount;
}

InfixTailReplayResult recover_stray_operator_run(RecoveryContext &ctx,
                                                 const InfixTailOps &ops,
                                                 std::int32_t minPrecedence) {
  ScopedBoolOverride skipPolicy{ctx.skipAfterDelete, false};
  bool stopTail = false;
  bool recovered = false;
  // Short-circuit is load-bearing: the side-effectful stray-operator scan
  // must run only when the cheap fast probe succeeds. Expressed as an
  // explicit `if` (rather than `probe && scan(...)`) so the side effect is
  // not buried in the right-hand operand of `&&`.
  if (ops.operatorFastProbe(ops.model, ctx, minPrecedence)) {
    recovered = scan_stray_operator_run(
        ctx, ops, minPrecedence, [&](const char *matchedCursor) {
          stopTail = ctx.skip_without_builder(matchedCursor) > matchedCursor;
        });
  }
  return {.matched = recovered, .stopTail = recovered && stopTail};
}

[[nodiscard]] std::optional<std::uint32_t>
observe_rhs_prefix_operator_noise_delete_count(RecoveryContext &ctx,
                                               const InfixTailOps &ops) {
  const auto checkpoint = ctx.mark();
  std::optional<std::uint32_t> observedDeleteCount;
  {
    // LocalRhsNoiseCleanup contract: skipAfterDelete=false (so the
    // bounded scan does not absorb a hidden separator) AND
    // allowDelete=true (so the probe works even when an outer
    // InsertRetry parent disabled deletes — InfixRule's RHS noise
    // delete is a grammar-driven affordance of the Pratt pattern,
    // bounded to one short of the global delete budget, not free
    // delete permission). Both mutations are visible in
    // `RecoveryPolicyFingerprint`.
    ScopedBoolOverride skipGuard{ctx.skipAfterDelete, false};
    auto editGuard = ctx.withEditPermissions(ctx.editPolicy.allowInsert, true);
    // The scan runs to the FULL delete budget so an operator-noise run
    // whose primary abuts it exactly at the budget is observed. The
    // conservative cap (one short of the budget) is re-applied below, but
    // ONLY to a primary reached across skipper trivia — that primary may
    // belong to the next construct, so it stays capped (the don't-fold-the-
    // next-statement invariant). A primary that abuts the noise with no
    // trivia is the glued operator-typo case and is absorbed up to the
    // full budget. Derive the cap from the budget so it auto-scales with
    // whatever budget the grammar/options configure.
    const auto maxProbeDeletes =
        ctx.editPolicy.maxConsecutiveCodepointDeletes > 1u
            ? ctx.editPolicy.maxConsecutiveCodepointDeletes - 1u
            : 1u;
    bool primaryAbutsNoise = false;
    // Set once the scan has crossed skipper trivia and landed on a
    // non-primary token: the deleted run is then no longer contiguous
    // with what follows, so we stop rather than keep deleting across the
    // separator (which would swallow a structural token like `;` and fold
    // the next construct into this RHS — a statement merge).
    bool crossedSeparatorWithoutPrimary = false;
    (void)recover_by_guarded_delete_scan(
        ctx,
        [&]() {
          // Delete a codepoint when it is a stray operator OR contiguous
          // non-primary garbage. Operators are safe to delete across
          // trivia (a typo operator run is often space-separated and is
          // never a structural anchor); arbitrary garbage is only deleted
          // while still contiguous (no separator crossed), so the scan
          // cannot walk past a `;` into the next construct. The strict-
          // primary matchFn below stops the scan the instant the operand
          // becomes reachable.
          if (ctx.cursor() == ctx.end) {
            return false;
          }
          if (ops.operatorFastProbe(ops.model, ctx, /*minPrecedence=*/0)) {
            return true;
          }
          return !crossedSeparatorWithoutPrimary &&
                 !ops.fastProbePrimary(ops.model, ctx);
        },
        [&](const char *) -> const char * {
          const char *const beforeSkip = ctx.cursor();
          ctx.skip();
          // The primary abuts the noise only when no skipper trivia was
          // crossed to reach it (the cursor did not advance on skip).
          const bool abutsHere = ctx.cursor() == beforeSkip;
          // Strict probe only: the purpose of the scan is to find a
          // position where `primary` strictly starts. Allowing
          // `probe_locally_recoverable` here would stop after a
          // single delete on any run of operator-like noise (the
          // recoverable probe is too permissive at operator
          // positions), leaving the remaining stray operators
          // unhandled by ContinueTail.
          if (ops.fastProbePrimary(ops.model, ctx)) {
            primaryAbutsNoise = abutsHere;
            return ctx.cursor();
          }
          // Landed on a non-primary token across trivia. If it is not a
          // stray operator either, it is a structural anchor (a `;`, a
          // closing token), so stop — the scan must not delete past it
          // into the next construct. A stray operator across trivia stays
          // deletable via the canDeleteStep above.
          if (!abutsHere &&
              !ops.operatorFastProbe(ops.model, ctx, /*minPrecedence=*/0)) {
            crossedSeparatorWithoutPrimary = true;
          }
          return nullptr;
        },
        [&](const char *, std::uint32_t deleteCount) {
          observedDeleteCount = deleteCount;
        },
        {.maxDeletes = ctx.editPolicy.maxConsecutiveCodepointDeletes,
         .allowOverflow = false});
    // Keep an over-cap observation only when the primary abuts the noise:
    // a glued operator-typo run cannot fold a following construct into
    // this RHS, whereas a primary reachable only across trivia (a space or
    // a newline) might, so it stays bounded by the conservative cap.
    if (observedDeleteCount.has_value() &&
        *observedDeleteCount > maxProbeDeletes && !primaryAbutsNoise) {
      observedDeleteCount.reset();
    }
  }
  ctx.rewind(checkpoint);
  return observedDeleteCount;
}

bool replay_rhs_prefix_operator_noise(RecoveryContext &ctx,
                                      const InfixTailOps &ops,
                                      std::uint32_t deleteCount) {
  if (deleteCount == 0u) {
    return true;
  }
  // Same policy as observe_rhs_prefix_operator_noise_delete_count:
  // the commit replay must see the same skipAfterDelete=false /
  // allowDelete=true scope so it commits exactly what the probe
  // sanctioned.
  ScopedBoolOverride skipGuard{ctx.skipAfterDelete, false};
  auto editGuard = ctx.withEditPermissions(ctx.editPolicy.allowInsert, true);
  return recover_by_guarded_delete_scan(
      ctx,
      [&]() {
        // Mirror observe_rhs_prefix_operator_noise_delete_count's relaxed
        // predicate so observe/replay delete the exact same run.
        return ctx.cursor() != ctx.end &&
               !ops.fastProbePrimary(ops.model, ctx);
      },
      [&](const char *, std::uint32_t deletedCount) {
        ctx.skip();
        return deletedCount == deleteCount ? ctx.cursor() : nullptr;
      },
      [](const char *) { /* no per-step progress action */ },
      {.maxDeletes = deleteCount, .allowOverflow = false});
}

bool observe_rhs_prefix_operator_noise(RecoveryContext &ctx,
                                       const InfixTailOps &ops,
                                       EditableTailObservation &observation) {
  const auto deleteCount =
      observe_rhs_prefix_operator_noise_delete_count(ctx, ops);
  if (!deleteCount.has_value()) {
    return false;
  }
  observation.rhsPrefixNoiseDeleteCount = *deleteCount;
  const bool replayedPrefixNoise = replay_rhs_prefix_operator_noise(
      ctx, ops, observation.rhsPrefixNoiseDeleteCount);
  assert(replayedPrefixNoise &&
         "Observed infix RHS prefix cleanup must replay during observation");
  return replayedPrefixNoise;
}

[[nodiscard]] EditableTailObservation
observe_editable_rhs_primary(RecoveryContext &ctx, const InfixTailOps &ops) {
  EditableTailObservation observation;
  ProbeRestoreScope guard{ctx};
  ctx.restoreMaxCursor(ctx.cursor());
  ctx.skip();
  // Enter the prefix-noise cleanup whenever the RHS primary does not
  // strictly start here — stray operators OR arbitrary garbage — so the
  // infix tail resyncs to the operand the same way recovery deletes noise
  // everywhere else.
  if (!ops.fastProbePrimary(ops.model, ctx) &&
      !observe_rhs_prefix_operator_noise(ctx, ops, observation)) {
    observation.exposedPrimary = false;
    return observation;
  }
  const char *const rhsStart = ctx.cursor();
  observation.rhsStartOffset = ctx.cursorOffset();
  const std::size_t baseRecoveryEditCount = ctx.recoveryEditCount();
  {
    ProbeRestoreScope noEditGuard{ctx};
    if (ops.parsePrimaryNoEdits(ops.model, ctx) && ctx.cursor() != rhsStart) {
      observation.matchedPrimary = true;
      observation.advancedPrimary = true;
      observation.rhsPrimaryObservedWithoutEdits = true;
      observation.postMatchCursorOffset = ctx.cursorOffset();
      return observation;
    }
  }
  observation.matchedPrimary = ops.parsePrimary(ops.model, ctx);
  observation.advancedPrimary = ctx.cursor() != rhsStart;
  observation.postMatchCursorOffset = ctx.cursorOffset();
  if (!observation.matchedPrimary || !observation.advancedPrimary) {
    return observation;
  }
  if (ctx.recoveryEditCount() == baseRecoveryEditCount) {
    return observation;
  }
  const auto edits = ctx.recoveryEditsView();
  observation.immediateEditCount = static_cast<std::uint32_t>(
      ctx.recoveryEditCount() - baseRecoveryEditCount);
  // Capture the first-edit offset and the aggregate edit cost so
  // the caller can project the ContinueTail candidate onto a
  // `RecoveryKey` after the rewind has rolled back the edits.
  observation.firstImmediateEditOffset = edits[baseRecoveryEditCount].offset;
  for (std::size_t i = baseRecoveryEditCount; i < ctx.recoveryEditCount();
       ++i) {
    observation.immediateEditCost += default_edit_cost(edits[i].kind);
  }
  // The InfixRule tail should match `many(op + Primary)` semantics: the
  // rhs primary is matched *starting at rhsStart*. Reject the ContinueTail
  // candidate when the speculative match instead looks like a grammar
  // reinterpretation, in either of two ways:
  //  - multiple edits all anchored at rhsStart, or
  //  - any edit committed *before* rhsStart, which back-edited an earlier
  //    token of the input to reshape it (e.g. inserting `(` at offset 23
  //    to reinterpret `c-4+;` as `c(-4)+;`).
  // Such multi-edit speculative cascades routinely beat the outer
  // recovery, so they must not win the tail here.
  observation.multipleImmediateEditsAtStart =
      (observation.immediateEditCount > 1u &&
       observation.firstImmediateEditOffset == observation.rhsStartOffset) ||
      observation.firstImmediateEditOffset < observation.rhsStartOffset;
  return observation;
}

[[nodiscard]] bool continue_editable_tail_candidate_is_legal(
    const EditableTailObservation &observation) noexcept {
  return observation.exposedPrimary && observation.matchedPrimary &&
         observation.advancedPrimary &&
         !observation.multipleImmediateEditsAtStart;
}

[[nodiscard]] std::array<EditableTailCandidate, 2>
enumerate_editable_tail_candidates(
    const EditableTailObservation &observation) noexcept {
  // ContinueTail is admissible exactly when a strict operator at the
  // cursor is followed by a strict-or-recoverable RHS primary; that
  // legality is computed by
  // `continue_editable_tail_candidate_is_legal(observation)`.
  const bool continueTailLegal =
      continue_editable_tail_candidate_is_legal(observation);
  // DeleteOperatorNoise is admissible exactly when the bounded
  // operator-noise scan committed a real delete count (which already
  // implies both a following strict RHS primary and an available
  // delete budget at the pre-operator cursor).
  const bool deleteNoiseLegal =
      observation.strayOperatorRunDeleteCount.has_value();

  return {{
      {.kind = InfixCandidateFamily::ContinueTail,
       .rhsPrefixNoiseDeleteCount = observation.rhsPrefixNoiseDeleteCount,
       .rhsPrimaryObservedWithoutEdits =
           observation.rhsPrimaryObservedWithoutEdits,
       .legal = continueTailLegal},
      {.kind = InfixCandidateFamily::DeleteOperatorNoise,
       .legal = deleteNoiseLegal},
  }};
}

/// Project each legal candidate onto the shared `RecoveryKey` and
/// select the best — the same comparator used across the rest of
/// recovery. Returns a pointer to the winning candidate, or nullptr
/// when none is legal.
///
/// ContinueTail's key comes from the observation (already computed,
/// costs captured before the speculative rewind).
///
/// DeleteOperatorNoise's key comes from the bounded, rewound
/// observation. The winner path still replays the scan before
/// committing edits, so observation and replay stay separate.
[[nodiscard]] const EditableTailCandidate *
select_editable_tail_candidate_by_recovery_key(
    const std::array<EditableTailCandidate, 2> &candidates,
    const EditableTailObservation &observation) {
  const EditableTailCandidate *selected = nullptr;
  RecoveryKey bestKey;
  bool anySelected = false;

  const auto considerCandidate = [&](const EditableTailCandidate &cand,
                                     const RecoveryKey &key) {
    if (!anySelected || is_better_recovery_key(key, bestKey)) {
      anySelected = true;
      bestKey = key;
      selected = std::addressof(cand);
    }
  };

  for (const auto &candidate : candidates) {
    if (!candidate.legal) {
      continue;
    }
    if (candidate.kind == InfixCandidateFamily::ContinueTail) {
      const auto effectiveFirstEdit = effective_first_edit_offset(
          observation.immediateEditCount != 0u,
          observation.firstImmediateEditOffset,
          observation.postMatchCursorOffset);
      const RecoveryKey key{
          .matched = observation.matchedPrimary,
          .firstEditOffset = effectiveFirstEdit,
          .editCost = observation.immediateEditCost,
          .editCount = observation.immediateEditCount,
          .progressAfterEdits = observation.postMatchCursorOffset,
      };
      considerCandidate(candidate, key);
    } else {
      // Real DeleteOperatorNoise cost from the observable scan
      // (`observe_stray_operator_run_delete_count`). The
      // candidate's `RecoveryKey` reflects the actual codepoint
      // count the committed `recover_stray_operator_run` would
      // produce. The candidate's `legal` flag (already filtered
      // above) is `strayOperatorRunDeleteCount.has_value()`, so the
      // optional is guaranteed engaged here.
      constexpr std::uint32_t kSingleDeleteCost =
          default_edit_cost(ParseDiagnosticKind::Deleted);
      const auto deleteCount = *observation.strayOperatorRunDeleteCount;
      const auto realEditCost = deleteCount * kSingleDeleteCost;
      const RecoveryKey key{
          .matched = true,
          // The scan's first edit lands at the operator position
          // (cursor at the start of the run, i.e.
          // `strayOperatorRunFirstEditOffset` captured before
          // `try_match_operator_editable` advanced the cursor).
          .firstEditOffset = observation.strayOperatorRunFirstEditOffset,
          .editCost = realEditCost,
          .editCount = deleteCount,
          .progressAfterEdits = observation.rhsStartOffset,
      };
      considerCandidate(candidate, key);
    }
  }
  return selected;
}

InfixTailReplayResult
replay_editable_rhs_primary(RecoveryContext &ctx, const InfixTailOps &ops,
                            const EditableTailCandidate &candidate,
                            std::int32_t nextMinPrecedence,
                            const char *lhsStart) {
  assert(candidate.kind == InfixCandidateFamily::ContinueTail &&
         "Only a ContinueTail candidate can replay an infix RHS");
  ctx.skip();
  const bool replayedPrefixNoise = replay_rhs_prefix_operator_noise(
      ctx, ops, candidate.rhsPrefixNoiseDeleteCount);
  assert(replayedPrefixNoise &&
         "Infix RHS prefix replay must match observed cleanup");
  if (!replayedPrefixNoise) {
    return {.matched = false, .stopTail = false};
  }
  const auto rhsStart = ctx.cursor();
  bool matchedPrimary = false;
  if (candidate.rhsPrimaryObservedWithoutEdits) {
    matchedPrimary = ops.parsePrimaryNoEdits(ops.model, ctx);
  } else {
    matchedPrimary = ops.parsePrimary(ops.model, ctx);
  }
  if (!matchedPrimary || ctx.cursor() == rhsStart) {
    // Speculative observation can disagree with replay when a
    // sibling recovery path mutates state (max cursor, failure
    // history, fingerprint-affecting edits) between the observation
    // and the replay. Reject the candidate and let the enclosing
    // recovery attempt compete normally instead of asserting.
    return {.matched = false, .stopTail = false};
  }
  ctx.skip();
  const auto nestedReplayResult =
      infix_parse_tail_editable(ctx, ops, nextMinPrecedence, rhsStart);
  assert(ops.owner && "The owner must be set");
  ctx.exit(lhsStart, ops.owner);
  return {.matched = true, .stopTail = nestedReplayResult.stopTail};
}

} // namespace

InfixTailReplayResult infix_parse_tail_editable(RecoveryContext &ctx,
                                                const InfixTailOps &ops,
                                                std::int32_t minPrecedence,
                                                const char *lhsStart) {
  if (minPrecedence > ops.maxPrecedence) {
    return {};
  }
  while (true) {
    if (!ops.operatorFastProbe(ops.model, ctx, minPrecedence)) {
      return {};
    }
    const auto nodeCheckpoint = ctx.mark();
    (void)ctx.enter();
    std::int32_t nextMinPrecedence = 0;
    if (!ops.matchOperatorEditable(ops.model, ctx, minPrecedence,
                                   nextMinPrecedence)) {
      ctx.rewind(nodeCheckpoint);
      return {};
    }
    ctx.skip();
    const auto rhsStart = ctx.cursor();
    const char *const directRhsSavedFurthest = ctx.maxCursor();
    if (ops.parsePrimaryNoEdits(ops.model, ctx) && ctx.cursor() != rhsStart) {
      ctx.skip();
      const auto nestedReplayResult =
          infix_parse_tail_editable(ctx, ops, nextMinPrecedence, rhsStart);
      assert(ops.owner && "The owner must be set");
      ctx.exit(lhsStart, ops.owner);
      if (!nestedReplayResult.matched || nestedReplayResult.stopTail) {
        return nestedReplayResult;
      }
      continue;
    }
    ctx.rewind(nodeCheckpoint);
    ctx.restoreMaxCursor(directRhsSavedFurthest);
    // Observable stray-operator scan from the pre-operator-match cursor
    // (the same cursor `recover_stray_operator_run` reads when
    // committed). It is deliberately lazy: when the RHS primary parses
    // strictly, no tail-recovery candidate is enumerated, so the scan
    // would only add recovery-mode cost to a nominal operator chain.
    const auto strayOperatorRunCursorOffset = ctx.cursorOffset();
    const auto strayOperatorRunDeleteCount =
        observe_stray_operator_run_delete_count(ctx, ops, minPrecedence);
    (void)ctx.enter();
    nextMinPrecedence = 0;
    if (!ops.matchOperatorEditable(ops.model, ctx, minPrecedence,
                                   nextMinPrecedence)) {
      ctx.rewind(nodeCheckpoint);
      return {};
    }
    auto observation = observe_editable_rhs_primary(ctx, ops);
    observation.strayOperatorRunDeleteCount = strayOperatorRunDeleteCount;
    observation.strayOperatorRunFirstEditOffset = strayOperatorRunCursorOffset;
    const auto candidates = enumerate_editable_tail_candidates(observation);
    const auto *selected =
        select_editable_tail_candidate_by_recovery_key(candidates, observation);
    if (selected == nullptr) {
      ctx.rewind(nodeCheckpoint);
      return {.matched = false, .stopTail = false};
    }
    if (selected->kind == InfixCandidateFamily::DeleteOperatorNoise) {
      ctx.rewind(nodeCheckpoint);
      return recover_stray_operator_run(ctx, ops, minPrecedence);
    }
    const auto replayResult = replay_editable_rhs_primary(
        ctx, ops, *selected, nextMinPrecedence, lhsStart);
    if (!replayResult.matched || replayResult.stopTail) {
      return replayResult;
    }
  }
}

} // namespace pegium::parser::detail
