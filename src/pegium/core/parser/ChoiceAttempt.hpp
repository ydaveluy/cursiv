#pragma once

/// The ordered-choice recovery driver (`run_choice_recovery`) and its
/// memoization cache.
///
/// The driver evaluates every branch of an ordered choice at an editable
/// descent point (no-edit and editable attempts, in declaration order),
/// ranks the resulting candidates, and replays the winner. It is compiled
/// once, here; each `OrderedChoice` instantiation only materializes a
/// small table of type-erased branch entry points (`ChoiceBranchOps`).
///
/// The cache captures the outcome of one branch enumeration as a
/// `ChoiceReplayMemo` so that repeated recovery descents at the same
/// `(choice, cursor, policy)` key skip the whole enumeration phase and
/// jump straight to the winner's replay.
///
/// Cache neutrality: the cache is only a pure optimization.
/// Disabling it via `ChoiceRecoverCache::setDisabled(true)` must never
/// change the chosen recovery candidate. This requires every recovery
/// policy axis that can influence branch enumeration to participate
/// in `ChoiceRecoverCacheKey`. `RecoveryPolicyFingerprint` centralises
/// those axes into a single typed struct so the key cannot drift out
/// of sync with the policy it memoizes.

#include <array>
#include <bit>
#include <cstdint>
#include <memory>
#include <span>

#include <pegium/core/parser/CandidateEnvelope.hpp>
#include <pegium/core/parser/RecoveryCandidate.hpp>
#include <pegium/core/utils/TextUtils.hpp>

namespace pegium::parser {
struct RecoveryContext;
} // namespace pegium::parser

namespace pegium::parser::detail {

enum class ChoiceAttemptKind : std::uint8_t {
  None,
  NoEditReplay,
  Editable,
};

/// One branch's candidacy during enumeration. Lives only for the duration
/// of `run_choice_recovery`'s enumeration loop; what survives into the
/// cache is the `ChoiceReplayMemo` projection.
struct ChoiceAttempt {
  std::size_t branchIndex = 0;
  ChoiceAttemptKind kind = ChoiceAttemptKind::None;
  /// Closed-vocabulary view consumed by admission, family-redundancy
  /// and ranking. Built via `to_candidate_envelope` at every construction
  /// site; the dispatch reads the envelope for decisions.
  CandidateEnvelope envelope{};
};

/// Memoized outcome of one branch enumeration: the winner's identity plus
/// the post-evaluation cumulative cursors. Those monotonic side effects
/// are produced by exploring all branches, but replaying the single winner
/// does not reproduce them, so cache hits must reapply them to keep the
/// downstream pipeline observing identical progress. Ranking data (the
/// candidate envelope) deliberately does not survive enumeration — replay
/// needs only the winner's identity.
struct ChoiceReplayMemo {
  std::uint32_t branchIndex = 0;
  ChoiceAttemptKind kind = ChoiceAttemptKind::None;
  TextOffset postEvalMaxCursorOffset = 0;
  TextOffset postEvalFurthestFailureOffset = 0;
  std::uint32_t postEvalFurthestVisibleLeafCount = 0;
};

/// Type-erased view of one ordered-choice branch: the element address plus
/// the two editable-mode entry points the recovery driver needs. Each
/// `OrderedChoice` instantiation materializes one array of these from its
/// element pack (static trampolines, per-instance element addresses).
struct ChoiceBranchOps {
  const void *element = nullptr;
  bool (*parseNoEdits)(const void *element, RecoveryContext &ctx) = nullptr;
  bool (*parseEditable)(const void *element, RecoveryContext &ctx) = nullptr;
};

/// Closed fingerprint of every recovery policy axis that can change the
/// evaluation outcome of an `OrderedChoice` branch enumeration.
///
/// Policy changes that occur mid-recovery (e.g. a scope that flips
/// `allowDelete`, a window that disables `allowLeadingTerminalInsertScope`,
/// a budget that drops `remainingEditBudget` below what an edit needs) must
/// force a cache miss, not silently reuse a cached winner computed under a
/// different policy. The fingerprint is the single source of truth for what
/// "same policy" means for the cache; adding a new policy axis to the
/// recovery context must also add it here, and failing to do so is a cache
/// neutrality bug.
///
/// Follow probes are identified by their function pointers plus the folded
/// CHAIN identity maintained by `FollowProbeGuard`
/// (`RecoveryContext::_followChainIdentity`): each installation folds its
/// probe fns, the installer's stable owner address and the enclosing chain's
/// identity into one word. The probes' raw data pointers deliberately do NOT
/// participate — they often address transient stack PODs whose addresses can
/// coincide across different descents (making raw pointers alias two
/// genuinely different follow contexts) while logically identical chains
/// rebuilt at different times would compare unequal (spurious misses).
struct RecoveryPolicyFingerprint {
  using FollowProbeFnPtr = const void *;

  FollowProbeFnPtr followProbeFn = nullptr;
  FollowProbeFnPtr recoverableFollowProbeFn = nullptr;
  FollowProbeFnPtr recoverableFollowConsumesVisibleProbeFn = nullptr;
  std::uint64_t followChainIdentity = 0;
  std::uint32_t remainingEditBudget = 0;
  std::uint32_t consecutiveDeletes = 0;
  TextOffset editFloorOffset = 0;
  bool allowInsert = false;
  bool allowDelete = false;
  bool skipAfterDelete = false;
  bool allowDestructiveWindowContinuation = false;
  bool allowLeadingTerminalInsertScope = false;
  bool inRecoveryPhase = false;
  bool hadEdits = false;
  bool insideEditWindow = false;
  bool completedWindowContinuation = false;
  // Read by the Repetition / UnorderedGroup / InfixRule recovery decisions.
  // A choice attempt cached at one value can replay a wrong winner if the
  // bit flips between visits.
  bool frontierBlocked = false;
  // Gates `insertSynthetic`/`deleteOneCodepoint`/`replaceLeaf` at every edit
  // site; flipped by `EditStateGuard` (e.g. `withEditTrackingDisabled`).
  bool trackEditState = false;
  // Index into committed-recovery-edits replay; controls whether edits are
  // admissible at the current cursor, read by `RecoveryContext`'s
  // `hasPendingCommittedRecoveryEdits()` admission gates.
  std::uint32_t committedRecoveryEditIndex = 0;
  // Per-attempt edit budgets, mutated by `ExtendedDeleteScanBudgetScope`'s
  // `tryEnable()` / `restore()`; affects which candidates are admissible.
  std::uint32_t remainingEditCount = 0;
  std::uint32_t remainingConsecutiveDeletes = 0;

  // Defaulted: every field participates in the cache key, so adding a
  // new field automatically extends the comparison. Manually written
  // comparisons silently drop a new field — a cache-poisoning footgun.
  [[nodiscard]] friend bool
  operator==(const RecoveryPolicyFingerprint &a,
             const RecoveryPolicyFingerprint &b) noexcept = default;
};

static_assert(std::is_trivially_copyable_v<RecoveryPolicyFingerprint>);

struct ChoiceRecoverCacheKey {
  const void *choice = nullptr;
  TextOffset cursorOffset = 0;
  TextOffset maxCursorOffset = 0;
  std::uint32_t furthestVisibleLeafCount = 0;
  std::uint32_t currentVisibleLeafCount = 0;
  RecoveryPolicyFingerprint policy{};

  [[nodiscard]] friend bool
  operator==(const ChoiceRecoverCacheKey &a,
             const ChoiceRecoverCacheKey &b) noexcept = default;
};

class ChoiceRecoverCache {
public:
  // Recovery-heavy malformed clusters can revisit the same ordered choices many
  // times within one parse window. A slightly larger cache avoids direct-mapped
  // thrash there without affecting nominal strict parses. The storage is heap-
  // allocated so that RecoveryContext instances stay small on the recursion
  // stack.
  static constexpr std::size_t kCapacity = 8192;
  static_assert(std::has_single_bit(kCapacity),
                "ChoiceRecoverCache capacity must be a power of two.");

  ChoiceRecoverCache();

  /// Toggles the cache off without dropping its storage. When disabled,
  /// `tryGet` always returns `nullptr` and the caller is forced to recompute
  /// every attempt. Stores still succeed (they are harmless) so re-enabling
  /// the cache does not require a warm-up pass. Used by the cache-neutrality
  /// test harness.
  void setDisabled(bool disabled) noexcept { _disabled = disabled; }

  [[nodiscard]] const ChoiceReplayMemo *
  tryGet(const ChoiceRecoverCacheKey &key) noexcept;

  void store(const ChoiceRecoverCacheKey &key,
             const ChoiceReplayMemo &value) noexcept;

  /// Clears every entry and the hit/miss counters without releasing the backing
  /// storage, re-establishing the exact state of a freshly constructed cache.
  /// Lets the recovery driver reuse one ~1.4 MB allocation across the several
  /// re-parses of a single window instead of allocating a cold cache per probe;
  /// because each probe still starts from an empty cache, behaviour is
  /// byte-for-byte identical to a per-attempt cache. Leaves `_disabled` untouched
  /// (callers set it explicitly per attempt).
  void reset() noexcept;

  [[nodiscard]] std::uint64_t hits() const noexcept { return _hits; }
  [[nodiscard]] std::uint64_t misses() const noexcept { return _misses; }

private:
  struct Entry {
    ChoiceRecoverCacheKey key{};
    ChoiceReplayMemo value{};
    // Liveness is decided by `generation == _generation` rather than a bool,
    // so `reset()` is an O(1) `++_generation` instead of an O(kCapacity) scan.
    // A fresh (or wrapped-around) cache leaves entries at generation 0 while
    // `_generation` starts at 1, so every slot reads stale until first written.
    std::uint32_t generation = 0;
  };

  std::unique_ptr<std::array<Entry, kCapacity>> _entries;
  // Current liveness epoch. Bumped by `reset()`. An entry is live iff its
  // `generation` equals this value. Starts at 1 so the all-zero entry state
  // produced by the constructor is stale. On wraparound back to 0 a one-time
  // full clear re-establishes the invariant (see `reset()`).
  std::uint32_t _generation = 1;
  std::uint64_t _hits = 0;
  std::uint64_t _misses = 0;
  bool _disabled = false;
};

/// Every recovery-policy axis that can change a branch enumeration outcome,
/// snapshotted from the live context. The single source of truth for what
/// "same policy" means for the cache key.
[[nodiscard]] RecoveryPolicyFingerprint
make_recovery_policy_fingerprint(const RecoveryContext &ctx) noexcept;

[[nodiscard]] ChoiceRecoverCacheKey
make_choice_recover_cache_key(const RecoveryContext &ctx,
                              const void *choice) noexcept;

/// Recovery driver for an ordered choice at an editable descent point:
/// consults the memo cache, otherwise evaluates every branch (no-edit and
/// editable attempts, in declaration order), ranks the candidates, stores
/// the winner, then replays it. `choiceIdentity` keys the cache (the
/// combinator's address). Returns the replay outcome; on failure the
/// context is rewound to its entry state.
[[nodiscard]] bool
run_choice_recovery(RecoveryContext &ctx, const void *choiceIdentity,
                    std::span<const ChoiceBranchOps> branches);

} // namespace pegium::parser::detail
