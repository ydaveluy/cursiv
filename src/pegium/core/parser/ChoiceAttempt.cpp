#include <pegium/core/parser/ChoiceAttempt.hpp>

#include <new>
#include <type_traits>

#include <pegium/core/parser/EditableRecoverySupport.hpp>
#include <pegium/core/parser/RecoveryContext.hpp>
#include <pegium/core/parser/RecoveryTrace.hpp>
#include <pegium/core/parser/StepTrace.hpp>

namespace pegium::parser::detail {

namespace {

[[nodiscard]] std::uint64_t
mix_policy(const RecoveryPolicyFingerprint &policy) noexcept {
  auto h = static_cast<std::uint64_t>(
      reinterpret_cast<std::uintptr_t>(policy.followProbeFn));
  h ^= static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(
           policy.recoverableFollowProbeFn)) *
       0xBF58476D1CE4E5B9ULL;
  h ^= static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(
           policy.recoverableFollowConsumesVisibleProbeFn)) *
       0x9E3779B185EBCA87ULL;
  h ^= policy.followChainIdentity * 0xC2B2AE3D27D4EB2FULL;
  // Every fingerprint axis that participates in the defaulted `operator==`
  // also participates in the hash. Skipping any axis here merely degrades
  // hit rate (collisions are filtered by the equality check), but keeping
  // hash and equality in lock-step makes adding a new axis a one-line edit
  // instead of a future-debugging session.
  h ^= static_cast<std::uint64_t>(policy.remainingEditBudget) *
       0xC2B2AE3D27D4EB4FULL;
  h ^= static_cast<std::uint64_t>(policy.consecutiveDeletes) *
       0x94D049BB133111EBULL;
  h ^= static_cast<std::uint64_t>(policy.editFloorOffset) *
       0x85EBCA77C2B2AE63ULL;
  h ^= static_cast<std::uint64_t>(policy.committedRecoveryEditIndex) *
       0xA29F0F39DD9E12C5ULL;
  h ^= static_cast<std::uint64_t>(policy.remainingEditCount) *
       0x4F2162926E40C299ULL;
  h ^= static_cast<std::uint64_t>(policy.remainingConsecutiveDeletes) *
       0x6E8E7A2F8B1B5C1DULL;
  const auto bools =
      (static_cast<std::uint32_t>(policy.allowInsert) << 0) |
      (static_cast<std::uint32_t>(policy.allowDelete) << 1) |
      (static_cast<std::uint32_t>(policy.skipAfterDelete) << 2) |
      (static_cast<std::uint32_t>(policy.allowDestructiveWindowContinuation)
       << 3) |
      (static_cast<std::uint32_t>(policy.allowLeadingTerminalInsertScope) << 4) |
      (static_cast<std::uint32_t>(policy.inRecoveryPhase) << 5) |
      (static_cast<std::uint32_t>(policy.hadEdits) << 6) |
      (static_cast<std::uint32_t>(policy.insideEditWindow) << 7) |
      (static_cast<std::uint32_t>(policy.completedWindowContinuation) << 8) |
      (static_cast<std::uint32_t>(policy.frontierBlocked) << 9) |
      (static_cast<std::uint32_t>(policy.trackEditState) << 10);
  h ^= static_cast<std::uint64_t>(bools) * 0xD1B54A32D192ED03ULL;
  return h;
}

[[nodiscard]] std::size_t slot(const ChoiceRecoverCacheKey &key) noexcept {
  auto h = static_cast<std::uint64_t>(
      reinterpret_cast<std::uintptr_t>(key.choice));
  h ^= static_cast<std::uint64_t>(key.cursorOffset) * 0x9E3779B97F4A7C15ULL;
  h ^= static_cast<std::uint64_t>(key.maxCursorOffset) * 0x3C79AC492BA7B653ULL;
  h ^= static_cast<std::uint64_t>(key.furthestVisibleLeafCount) *
       0x6A5D39EAE116586DULL;
  h ^= static_cast<std::uint64_t>(key.currentVisibleLeafCount) *
       0x3A1F5B8D2C7E9011ULL;
  h ^= mix_policy(key.policy);
  h ^= h >> 33;
  h *= 0xFF51AFD7ED558CCDULL;
  h ^= h >> 33;
  return static_cast<std::size_t>(h) & (ChoiceRecoverCache::kCapacity - 1);
}

} // namespace

ChoiceRecoverCache::ChoiceRecoverCache() {
  // Allocate uninitialised storage and only initialise the `generation`
  // field of each entry to 0 (stale). Keys and values are not constructed;
  // tryGet rejects every slot whose `generation != _generation` before
  // reading any other field, so the uninit key/value bytes are never
  // observed in production. store() overwrites both before stamping
  // `generation`, fully constructing the entry. Saves ~1.4 MB of memset
  // per cache construction (~12% of total Ir on recovery-heavy workloads
  // in callgrind). `_generation` starts at 1 (see header) so the all-zero
  // entries read stale on the first probe.
  static_assert(std::is_trivially_destructible_v<Entry>);
  void *const raw = ::operator new(sizeof(std::array<Entry, kCapacity>));
  _entries.reset(static_cast<std::array<Entry, kCapacity> *>(raw));
  for (auto &entry : *_entries) {
    entry.generation = 0;
  }
}

const ChoiceReplayMemo *
ChoiceRecoverCache::tryGet(const ChoiceRecoverCacheKey &key) noexcept {
  if (_disabled) {
    ++_misses;
    return nullptr;
  }
  const auto index = slot(key);
  const auto &entry = (*_entries)[index];
  if (entry.generation != _generation || !(entry.key == key)) {
    ++_misses;
    return nullptr;
  }
  ++_hits;
  return &entry.value;
}

void ChoiceRecoverCache::store(const ChoiceRecoverCacheKey &key,
                               const ChoiceReplayMemo &value) noexcept {
  const auto index = slot(key);
  auto &entry = (*_entries)[index];
  entry.key = key;
  entry.value = value;
  entry.generation = _generation;
}

void ChoiceRecoverCache::reset() noexcept {
  // O(1) invalidation: bumping the epoch makes every previously-stamped entry
  // read stale (its `generation` no longer matches `_generation`). Only on
  // wraparound back to 0 must we re-zero the entries, because then a fresh
  // store at generation 0 would collide with the never-written sentinel; the
  // one-time full clear restores the invariant (mirrors the fuzzy cache's
  // wraparound handling). After the clear, restart the epoch at 1 so the
  // freshly zeroed entries stay stale.
  if (++_generation == 0) {
    for (auto &entry : *_entries) {
      entry.generation = 0;
    }
    _generation = 1;
  }
  _hits = 0;
  _misses = 0;
}

RecoveryPolicyFingerprint
make_recovery_policy_fingerprint(const RecoveryContext &ctx) noexcept {
  RecoveryPolicyFingerprint fp;
  fp.followProbeFn =
      reinterpret_cast<const void *>(ctx._followProbes.strict.fn);
  fp.recoverableFollowProbeFn =
      reinterpret_cast<const void *>(ctx._followProbes.recoverable.fn);
  fp.recoverableFollowConsumesVisibleProbeFn = reinterpret_cast<const void *>(
      ctx._followProbes.recoverableConsumesVisible.fn);
  fp.followChainIdentity = ctx._followChainIdentity;
  fp.remainingEditBudget =
      ctx.editPolicy.maxEditCost > ctx.currentEditCost()
          ? ctx.editPolicy.maxEditCost - ctx.currentEditCost()
          : 0U;
  fp.consecutiveDeletes = ctx.recoveryState.editBudget.consecutiveDeletes;
  fp.editFloorOffset = ctx.editPolicy.editFloorOffset;
  fp.allowInsert = ctx.editPolicy.allowInsert;
  fp.allowDelete = ctx.editPolicy.allowDelete;
  fp.skipAfterDelete = ctx.skipAfterDelete;
  fp.allowDestructiveWindowContinuation =
      ctx.allowDestructiveWindowContinuation;
  fp.allowLeadingTerminalInsertScope = ctx.allowLeadingTerminalInsertScope;
  fp.inRecoveryPhase = ctx.isInRecoveryPhase();
  fp.hadEdits = ctx.recoveryState.editBudget.hadEdits;
  fp.insideEditWindow = ctx.editWindow.has_value();
  fp.completedWindowContinuation =
      ctx.allowsCompletedWindowContinuationRecovery();
  fp.frontierBlocked = ctx.frontierBlocked();
  fp.trackEditState = ctx.editPolicy.trackEditState;
  fp.committedRecoveryEditIndex = ctx.recoveryState.committedRecoveryEditIndex;
  fp.remainingEditCount =
      ctx.editPolicy.maxEditsPerAttempt > ctx.recoveryState.editBudget.editCount
          ? ctx.editPolicy.maxEditsPerAttempt -
                ctx.recoveryState.editBudget.editCount
          : 0U;
  fp.remainingConsecutiveDeletes =
      ctx.editPolicy.maxConsecutiveCodepointDeletes >
              ctx.recoveryState.editBudget.consecutiveDeletes
          ? ctx.editPolicy.maxConsecutiveCodepointDeletes -
                ctx.recoveryState.editBudget.consecutiveDeletes
          : 0U;
  return fp;
}

ChoiceRecoverCacheKey
make_choice_recover_cache_key(const RecoveryContext &ctx,
                              const void *choice) noexcept {
  ChoiceRecoverCacheKey key;
  key.choice = choice;
  key.cursorOffset = ctx.cursorOffset();
  key.maxCursorOffset = ctx.maxCursorOffset();
  key.furthestVisibleLeafCount =
      static_cast<std::uint32_t>(ctx.furthestFailureHistorySize());
  key.currentVisibleLeafCount =
      static_cast<std::uint32_t>(ctx.failureHistorySize());
  key.policy = make_recovery_policy_fingerprint(ctx);
  return key;
}

namespace {

/// Captures the no-edit branch candidate. Only called after the branch's
/// no-edit attempt matched, so no recovery edits were committed: passing the
/// live cost/count as the bases makes the shared constructor observe an empty
/// edit slice (editCost / editCount zero, replayPrefix Empty).
[[nodiscard]] EditableRecoveryCandidate
capture_choice_candidate(RecoveryContext &ctx) {
  return capture_editable_candidate(ctx, ctx.currentEditCost(),
                                    ctx.recoveryEditCount(),
                                    KeyProgressConvention::PostSkipCursor);
}

/// Selects the better of two choice attempts by routing both envelopes
/// through the shared `consider_candidate_envelope` kernel with
/// `FamilyRedundancyPolicy::ChoiceAnchorExtension`: admission on
/// `envelope.key.matched`, then a one-directional `extension_dominates`
/// take-arm (an extending candidate — same anchor,
/// `ReplayPrefixClass::ExtendedCommittedPrefix`, no-worse cost, strictly
/// further progress, gated by a `progressGap >= costGap` ratio test —
/// outranks a base candidate; NOT a replay-equivalence dominance, the two
/// candidates carry different scripts), then `is_better_recovery_key`
/// ranking. `bestAttempt.envelope.key.matched` is the incumbent-validity
/// flag the kernel needs.
void consider_choice_attempt(ChoiceAttempt &bestAttempt,
                             const ChoiceAttempt &candidate) noexcept {
  if (consider_candidate_envelope(
          candidate.envelope, bestAttempt.envelope,
          bestAttempt.envelope.key.matched,
          FamilyRedundancyPolicy::ChoiceAnchorExtension) ==
      SelectionVerdict::Take) {
    bestAttempt = candidate;
  }
}

} // namespace

bool run_choice_recovery(RecoveryContext &ctx, const void *choiceIdentity,
                         std::span<const ChoiceBranchOps> branches) {
  PEGIUM_STEP_TRACE_INC(StepCounter::ChoiceRecoverCalls);

  const auto entryCheckpoint = ctx.mark();
  PEGIUM_RECOVERY_TRACE("[choice rule] enter offset=", ctx.cursorOffset(),
                        " allowI=", ctx.editPolicy.allowInsert,
                        " allowD=", ctx.editPolicy.allowDelete);

  const auto cacheKey = make_choice_recover_cache_key(ctx, choiceIdentity);
  ChoiceReplayMemo memo;
  if (const auto *cached = ctx.choiceRecoverCache().tryGet(cacheKey)) {
    memo = *cached;
    ctx.bumpMaxCursor(ctx.begin + memo.postEvalMaxCursorOffset);
    ctx.bumpFurthestFailureHistorySize(memo.postEvalFurthestVisibleLeafCount);
    ctx.bumpFurthestFailureOffset(memo.postEvalFurthestFailureOffset);
  } else {
    const auto baseEditCost = ctx.currentEditCost();
    const auto baseRecoveryEditCount = ctx.recoveryEditCount();
    ChoiceAttempt bestAttempt;
    for (std::size_t index = 0; index < branches.size(); ++index) {
      const ChoiceBranchOps &branch = branches[index];
      ChoiceAttempt noEditAttempt{.branchIndex = index};
      if (branch.parseNoEdits(branch.element, ctx)) {
        const auto candidate = capture_choice_candidate(ctx);
        PEGIUM_RECOVERY_TRACE("[choice rule] strict success offset=",
                              ctx.cursorOffset());
        noEditAttempt.kind = ChoiceAttemptKind::NoEditReplay;
        noEditAttempt.envelope = to_candidate_envelope(candidate);
      }
      ctx.rewind(entryCheckpoint);
      consider_choice_attempt(bestAttempt, noEditAttempt);

      const auto editableCandidate = evaluate_editable_recovery_candidate(
          ctx, entryCheckpoint, baseEditCost, baseRecoveryEditCount,
          [&]() { return branch.parseEditable(branch.element, ctx); });
      consider_choice_attempt(
          bestAttempt,
          ChoiceAttempt{
              .branchIndex = index,
              .kind = ChoiceAttemptKind::Editable,
              .envelope = to_candidate_envelope(editableCandidate),
          });
    }
    PEGIUM_STEP_TRACE_INC(StepCounter::ChoiceStrictPasses);
    PEGIUM_STEP_TRACE_INC(StepCounter::ChoiceEditablePasses);
    memo.branchIndex = static_cast<std::uint32_t>(bestAttempt.branchIndex);
    memo.kind = bestAttempt.kind;
    memo.postEvalMaxCursorOffset = ctx.maxCursorOffset();
    memo.postEvalFurthestVisibleLeafCount =
        static_cast<std::uint32_t>(ctx.furthestFailureHistorySize());
    memo.postEvalFurthestFailureOffset = ctx.furthestFailureOffset();
    ctx.choiceRecoverCache().store(cacheKey, memo);
  }

  switch (memo.kind) {
  case ChoiceAttemptKind::NoEditReplay: {
    const ChoiceBranchOps &branch = branches[memo.branchIndex];
    if (branch.parseNoEdits(branch.element, ctx)) {
      PEGIUM_RECOVERY_TRACE("[choice rule] deferred strict success offset=",
                            ctx.cursorOffset());
      return true;
    }
    ctx.rewind(entryCheckpoint);
    break;
  }
  case ChoiceAttemptKind::Editable: {
    const ChoiceBranchOps &branch = branches[memo.branchIndex];
    if (branch.parseEditable(branch.element, ctx)) {
      PEGIUM_RECOVERY_TRACE("[choice rule] editable success offset=",
                            ctx.cursorOffset());
      return true;
    }
    ctx.rewind(entryCheckpoint);
    break;
  }
  case ChoiceAttemptKind::None:
    break;
  }

  PEGIUM_RECOVERY_TRACE("[choice rule] fail offset=", ctx.cursorOffset());
  return false;
}

} // namespace pegium::parser::detail
