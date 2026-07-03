#pragma once

/// The bounded-edit error-recovery parse context.

#include <pegium/core/parser/ParseContext.hpp>

namespace pegium::parser {

struct RecoveryContext : TrackedParseContext {
  struct EditWindow {
    TextOffset beginOffset = 0;
    TextOffset editFloorOffset = 0;
    TextOffset maxCursorOffset = 0;
    std::uint32_t forwardTokenCount = 0;
    std::uint32_t replayForwardTokenCount = 1;
  };

  using RecoveryEdit = detail::SyntaxScriptEntry;

  using EditBudgetState = detail::EditBudget;

  struct WindowReplayState {
    std::uint32_t activeWindowEditCostBase = 0;
    std::uint32_t activeWindowEditCountBase = 0;
    std::uint32_t currentForwardVisibleLeafCount = 0;
    std::uint32_t strictVisibleLeafCountAfterRecovery = 0;
    std::uint32_t completedRecoveryWindows = 0;
    bool inRecoveryPhase = true;
    bool reachedRecoveryTarget = false;
    bool stableAfterRecovery = false;
    bool awaitingStrictStability = false;
    bool recoveryBookkeepingEnabled = true;
    bool activeEditWindowCompleted = false;
  };

  struct DeleteBridgeState {
    const char *pendingHiddenTriviaStart = nullptr;
    const char *pendingHiddenTriviaEnd = nullptr;
  };

  /// Every checkpointed recovery satellite that `rewind` restores wholesale
  /// by copying the struct back. State that is deliberately never rewound
  /// (the committed replay script, `recoveryRuleEntries`) lives outside.
  struct RecoveryState {
    EditBudgetState editBudget{};
    WindowReplayState windowReplay{};
    DeleteBridgeState deleteBridge{};
    /// Index of the next committed replay edit (into
    /// `committedScript.edits`). Checkpointed: a rewound speculative branch
    /// must re-replay the committed edits it consumed.
    std::uint32_t committedRecoveryEditIndex = 0;
    bool frontierBlocked = false;
  };

  static_assert(std::is_trivially_copyable_v<EditBudgetState>);
  static_assert(std::is_trivially_copyable_v<WindowReplayState>);
  static_assert(std::is_trivially_copyable_v<DeleteBridgeState>);
  static_assert(std::is_trivially_copyable_v<RecoveryState>);

  struct Checkpoint {
    TrackedParseContext::Checkpoint parseCheckpoint;
    RecoveryState recoveryState;
    std::uint32_t recoveryEditCount;
    std::uint32_t editWindowReplayForwardTokenCount;

    [[nodiscard]] constexpr const char *cursor() const noexcept {
      return parseCheckpoint.parseCheckpoint.cursor;
    }
  };

  using EditStateGuard = detail::EditStateGuard<RecoveryContext>;
  using ActiveRecoveryGuard = detail::ActiveRecoveryStack::Guard<RecoveryContext>;
  RecoveryContext(
      CstBuilder &builder, const Skipper &skipper,
      detail::FailureHistoryRecorder &failureRecorder,
      const utils::CancellationToken &cancelToken =
          utils::default_cancel_token) noexcept
      : TrackedParseContext(builder, skipper, failureRecorder, cancelToken) {
    _runRecoveryBookkeeping = true;
  }

  /// Editable-permission and edit-limit knobs shared field-for-field with
  /// `ExpectContext` (see `detail::EditPolicy`). Everything below it is a
  /// recovery-only exploration knob.
  detail::EditPolicy editPolicy;
  /// Word/identifier-character classification shared field-for-field with
  /// `ExpectContext` (see `detail::WordCharMask`).
  detail::WordCharMask wordChars;
  bool skipAfterDelete = true;
  std::uint32_t stabilityTokenCount = kDefaultRecoveryStabilityTokenCount;
  std::uint32_t maxResyncSkipCodepoints = kDefaultMaxResyncSkipCodepoints;
  /// When set, the out-of-window single-fuzzy-edit keyword Replace carve-out in
  /// `replaceLeaf` is forced OFF (mirror of
  /// `RecoveryAttemptSpec::probeAxes.forbidOutOfWindowFuzzyFold`, set only by
  /// the no-fold sibling probe). Threaded in `execute_recovery_parse`; read in
  /// the `replaceLeaf` carve-out below.
  bool forbidOutOfWindowFuzzyFold = false;
  /// When set, the fuzzy whole-keyword Replace candidate is suppressed in
  /// `Literal::try_local_recovery` whenever a boundary split-Insert is
  /// also enumerable (mirror of
  /// `RecoveryAttemptSpec::probeAxes.forbidWholeKeywordFuzzyReplace`, set only
  /// by the forbid-Replace sibling probe). Threaded in `execute_recovery_parse`;
  /// read in `Literal::try_local_recovery`.
  bool forbidWholeKeywordFuzzyReplace = false;
  /// Hard cap on the cumulative number of `ParserRule` recovery entries
  /// during a single recovery window. A pathological grammar shape (e.g.
  /// unclosed nested call expressions) can drive `evaluate_editable_recovery_candidate`
  /// into an exponentially-branching tree of speculative parses; counting
  /// every entry and aborting once the cap is hit collapses that exploration
  /// so the outer driver falls back to a less ambitious candidate. Reset to
  /// 0 implicitly because every `try_recovery_window` creates a fresh
  /// `RecoveryContext`.
  std::uint32_t maxRecoveryRuleEntries = 12000;
  std::uint32_t recoveryRuleEntries = 0;

  // Hard cap on `ParserRule` recovery recursion depth. Adversarial inputs
  // can produce deeply-nested grammar instantiations (e.g. a graph language
  // with `Map { entry: Map { entry: Map { ... } } }`) where every level
  // adds ~5 stack frames in ASan/sancov-instrumented builds; without a
  // cap, the FuzzTest watchdog (128 KiB stack budget) trips on otherwise-
  // benign inputs. 24 levels keeps the worst observed adversarial fuzz
  // input under ~93 KiB of stack while still covering realistic
  // hand-written programs (the strict path is uncapped).
  static constexpr std::uint16_t kMaxRecoveryRuleDepth = 24;
  std::uint16_t recoveryRuleDepth = 0;
  std::optional<EditWindow> editWindow;
  std::vector<RecoveryEdit> recoveryEdits;

  /// Edits accepted by earlier recovery windows that the current parse
  /// replays verbatim before it may spend fresh edits. Deliberately never
  /// rewound: the script is the fixed contract of the whole attempt, and the
  /// per-branch replay position lives in
  /// `recoveryState.committedRecoveryEditIndex` (which IS checkpointed).
  struct CommittedReplayScript {
    std::vector<RecoveryEdit> edits;
    /// Offset before which the next window may not begin.
    TextOffset resumeFloor = 0;
  };
  CommittedReplayScript committedScript;
  RecoveryState recoveryState;
  bool allowDestructiveWindowContinuation = false;
  bool allowLeadingTerminalInsertScope = false;

  // Recovery memoization caches (ordered-choice attempts + fuzzy-keyword DP
  // results). Owned lazily so a context that never reaches choice/fuzzy
  // recovery allocates neither, and so the recovery driver can install pooled
  // instances via `usePooledRecoveryCaches` that are shared across the several
  // re-parses of one window — otherwise each ~1.4 MB choice cache and ~256 KB
  // fuzzy cache is allocated cold on every probe (primary + minimize + minimal
  // + alternative). The accessors lazily build an owned cache when none is
  // installed; pooled callers bind shared instances instead. Lifetimes still
  // track one parse exactly (stable grammar-literal / text-snapshot pointers),
  // so these must never be promoted to a global or thread-local — see
  // `LiteralFuzzyCandidatesCache` documentation.
  std::unique_ptr<detail::ChoiceRecoverCache> ownedChoiceRecoverCache;
  std::unique_ptr<detail::LiteralFuzzyCandidatesCache>
      ownedLiteralFuzzyCandidatesCache;
  detail::ChoiceRecoverCache *pooledChoiceRecoverCache = nullptr;
  detail::LiteralFuzzyCandidatesCache *pooledLiteralFuzzyCandidatesCache =
      nullptr;

  /// Binds externally-owned, pooled recovery caches to this context so a single
  /// allocation is reused across one window's re-parses. A null argument leaves
  /// that cache lazily owned. Must be called before the first cache access.
  void usePooledRecoveryCaches(
      detail::ChoiceRecoverCache *choice,
      detail::LiteralFuzzyCandidatesCache *fuzzy) noexcept {
    pooledChoiceRecoverCache = choice;
    pooledLiteralFuzzyCandidatesCache = fuzzy;
  }

  [[nodiscard]] detail::ChoiceRecoverCache &choiceRecoverCache() {
    if (pooledChoiceRecoverCache != nullptr) {
      return *pooledChoiceRecoverCache;
    }
    if (!ownedChoiceRecoverCache) {
      ownedChoiceRecoverCache = std::make_unique<detail::ChoiceRecoverCache>();
    }
    return *ownedChoiceRecoverCache;
  }

  [[nodiscard]] detail::LiteralFuzzyCandidatesCache &
  literalFuzzyCandidatesCache() {
    if (pooledLiteralFuzzyCandidatesCache != nullptr) {
      return *pooledLiteralFuzzyCandidatesCache;
    }
    if (!ownedLiteralFuzzyCandidatesCache) {
      ownedLiteralFuzzyCandidatesCache =
          std::make_unique<detail::LiteralFuzzyCandidatesCache>();
    }
    return *ownedLiteralFuzzyCandidatesCache;
  }

  using FollowProbeFn = bool (*)(RecoveryContext &, const void *);

  /// The live follow-probe channel installed by the enclosing Group /
  /// Repetition scope. The three slots are three DISTINCT predicates —
  /// strict acceptance, recoverable continuation, and recoverable
  /// continuation that commits visible source — packed into one POD so
  /// `FollowProbeGuard` saves and restores the channel wholesale.
  struct FollowProbes {
    struct Slot {
      FollowProbeFn fn = nullptr;
      const void *data = nullptr;
    };
    Slot strict;
    Slot recoverable;
    Slot recoverableConsumesVisible;
  };
  static_assert(std::is_trivially_copyable_v<FollowProbes>);

  FollowProbes _followProbes;
  /// Stable identity of the live follow-probe CHAIN, folded at every guard
  /// installation from (probe fns, the installer's stable owner address, the
  /// enclosing chain's identity). This is what the choice-recover cache
  /// fingerprint keys on: the probe DATA pointers often address transient
  /// stack PODs whose contents (the captured outer chain) differ while their
  /// addresses coincide across descents, so raw data pointers under- and
  /// over-discriminate at the same time. Two logically identical chains fold
  /// to the same identity (cache reuse); two different chains differ (no
  /// false hits), independent of stack layout. 0 = no follow installed.
  std::uint64_t _followChainIdentity = 0;

  [[nodiscard]] static std::uint64_t fold_follow_chain_identity(
      std::uint64_t outerIdentity, FollowProbeFn fn, FollowProbeFn recoverableFn,
      FollowProbeFn recoverableConsumesVisibleFn,
      const void *stableOwner) noexcept {
    const auto mixOne = [](std::uint64_t h, std::uint64_t v) noexcept {
      h ^= v + 0x9E3779B97F4A7C15ULL + (h << 6U) + (h >> 2U);
      return h;
    };
    std::uint64_t h = outerIdentity;
    h = mixOne(h, reinterpret_cast<std::uintptr_t>(fn));
    h = mixOne(h, reinterpret_cast<std::uintptr_t>(recoverableFn));
    h = mixOne(h, reinterpret_cast<std::uintptr_t>(recoverableConsumesVisibleFn));
    h = mixOne(h, reinterpret_cast<std::uintptr_t>(stableOwner));
    return h;
  }

  // Returns true when the immediately-enclosing Group's successor element would
  // accept the current cursor. Grammar-agnostic: relies only on structural
  // probing installed by Group around each element's parse.
  [[nodiscard]] bool probeFollowAcceptsHere() noexcept {
    const auto &slot = _followProbes.strict;
    if (slot.fn == nullptr) {
      return false;
    }
    return slot.fn(*this, slot.data);
  }

  // Returns true when the immediately-enclosing Group's successor can recover
  // locally from the current cursor, even if it does not accept strictly yet.
  [[nodiscard]] bool probeRecoverableFollowHere() noexcept {
    const auto &slot = _followProbes.recoverable;
    if (slot.fn == nullptr) {
      return false;
    }
    return slot.fn(*this, slot.data);
  }

  // Stronger recoverable-follow probe: true only when the enclosing suffix can
  // recover from this cursor and commit visible source after the recovery site.
  // Repetition uses this to avoid stealing a parent boundary with a synthetic
  // local separator when the parent can prove real continuation.
  [[nodiscard]] bool probeRecoverableFollowConsumesVisibleHere() noexcept {
    const auto &slot = _followProbes.recoverableConsumesVisible;
    if (slot.fn == nullptr) {
      return false;
    }
    return slot.fn(*this, slot.data);
  }

  // The follow probe guard always installs a LOCAL probe: the new probe
  // replaces the outer probe for its scope; queries do not chain. Used for
  // non-last Group elements where the immediate successor is the only valid
  // follow (chaining to the OUTER would let outer-scope continuations mask
  // in-group failures). The LAST element of a Group installs NO guard at all
  // (Group::with_current_follow_probe) so the enclosing scope's follow stays
  // live — which is exactly the semantic follow the last element needs.
  struct [[nodiscard("a discarded FollowProbeGuard temporary tears the follow "
                     "probe down immediately")]] FollowProbeGuard {
    FollowProbeGuard(RecoveryContext &c, FollowProbeFn fn, const void *data,
                     FollowProbeFn recoverableFn = nullptr,
                     const void *recoverableData = nullptr,
                     FollowProbeFn recoverableConsumesVisibleFn = nullptr,
                     const void *recoverableConsumesVisibleData = nullptr,
                     const void *stableOwner = nullptr) noexcept
        : ctx(&c), prevProbes(c._followProbes),
          prevChainIdentity(c._followChainIdentity) {
      c._followProbes = {
          .strict = {fn, data},
          .recoverable = {recoverableFn, recoverableData},
          .recoverableConsumesVisible = {recoverableConsumesVisibleFn,
                                         recoverableConsumesVisibleData}};
      // An all-null install (withNoFollowProbe) resets the chain identity to
      // the no-follow state: every cleared-follow scope behaves identically,
      // so they must share one identity for cache reuse.
      c._followChainIdentity =
          (fn == nullptr && recoverableFn == nullptr &&
           recoverableConsumesVisibleFn == nullptr)
              ? 0
              : fold_follow_chain_identity(prevChainIdentity, fn, recoverableFn,
                                           recoverableConsumesVisibleFn,
                                           stableOwner);
    }
    FollowProbeGuard(const FollowProbeGuard &) = delete;
    FollowProbeGuard &operator=(const FollowProbeGuard &) = delete;
    FollowProbeGuard(FollowProbeGuard &&) = delete;
    FollowProbeGuard &operator=(FollowProbeGuard &&) = delete;
    ~FollowProbeGuard() noexcept {
      if (ctx) {
        ctx->_followProbes = prevProbes;
        ctx->_followChainIdentity = prevChainIdentity;
      }
    }

  private:
    RecoveryContext *ctx;
    FollowProbes prevProbes;
    std::uint64_t prevChainIdentity;
  };

  [[nodiscard]] FollowProbeGuard
  withFollowProbe(FollowProbeFn fn, const void *data,
                  FollowProbeFn recoverableFn = nullptr,
                  const void *recoverableData = nullptr,
                  FollowProbeFn recoverableConsumesVisibleFn = nullptr,
                  const void *recoverableConsumesVisibleData = nullptr,
                  const void *stableOwner = nullptr) noexcept {
    return FollowProbeGuard{*this,
                            fn,
                            data,
                            recoverableFn,
                            recoverableData,
                            recoverableConsumesVisibleFn,
                            recoverableConsumesVisibleData,
                            stableOwner};
  }

  /// Installs a fully-null follow probe, clearing every follow query
  /// for the scope of the returned guard. Equivalent to passing all-null
  /// arguments to `withFollowProbe` (the remaining arguments default to
  /// nullptr).
  [[nodiscard]] FollowProbeGuard withNoFollowProbe() noexcept {
    return withFollowProbe(nullptr, nullptr);
  }

  inline void refreshRecoveryPhase() noexcept {
    const auto &windowReplay = recoveryState.windowReplay;
    if (!windowReplay.recoveryBookkeepingEnabled) [[likely]] {
      return;
    }
    refreshRecoveryPhaseSlow();
  }

private:
  void refreshRecoveryPhaseSlow() noexcept;

public:

  [[nodiscard]] inline Checkpoint mark() const noexcept {
    return {
        .parseCheckpoint = TrackedParseContext::mark(),
        .recoveryState = recoveryState,
        .recoveryEditCount = static_cast<std::uint32_t>(recoveryEdits.size()),
        .editWindowReplayForwardTokenCount =
            editWindow.has_value() ? editWindow->replayForwardTokenCount : 0u};
  }

  using TrackedParseContext::rewind;

  inline void rewind(const Checkpoint &checkpoint) noexcept {
    TrackedParseContext::rewind(checkpoint.parseCheckpoint);
    if (editWindow.has_value()) [[likely]] {
      editWindow->replayForwardTokenCount =
          checkpoint.editWindowReplayForwardTokenCount;
    }
    recoveryState = checkpoint.recoveryState;
    // `recoveryRuleEntries` is intentionally NOT rewound: the cap is a
    // per-window global budget on speculative ParserRule entries, and a
    // rewind to a sibling branch must continue to see the total work
    // already consumed. Otherwise sibling branches would each get fresh
    // budget and the sum would explode exponentially on pathological
    // grammar shapes (the very thing the cap exists to bound).
    // Speculative parses that didn't push an edit dominate the rewind path;
    // skip the resize call (and the underlying destructor loop / size-store
    // sequence) when the count is unchanged.
    if (recoveryEdits.size() != checkpoint.recoveryEditCount) [[unlikely]] {
      recoveryEdits.resize(checkpoint.recoveryEditCount);
    }
  }

  inline void exit(const char *checkpoint,
                   const grammar::AbstractElement *element) {
    TrackedParseContext::exit(checkpoint, element);
    if (editPolicy.trackEditState &&
        recoveryState.editBudget.consecutiveDeletes != 0) [[unlikely]] {
      recoveryState.editBudget.consecutiveDeletes = 0;
    }
  }

  [[nodiscard]] constexpr bool frontierBlocked() const noexcept {
    return recoveryState.frontierBlocked;
  }

  void clearFrontierBlock() noexcept { recoveryState.frontierBlocked = false; }

  void blockFrontier() noexcept { recoveryState.frontierBlocked = true; }

  void setEditWindow(std::optional<EditWindow> window) noexcept {
    editWindow = std::move(window);
    recoveryState.windowReplay = WindowReplayState{
        .activeWindowEditCostBase = recoveryState.editBudget.editCost,
        .activeWindowEditCountBase = recoveryState.editBudget.editCount,
        .inRecoveryPhase = false,
        .recoveryBookkeepingEnabled = editWindow.has_value(),
    };
    _recordFailureHistory = recoveryState.windowReplay.recoveryBookkeepingEnabled;
    editPolicy.editFloorOffset =
        editWindow.has_value() ? editWindow->editFloorOffset : TextOffset{0};
    recoveryState.deleteBridge = {};
  }

  void setCommittedRecoveryPrefix(std::vector<RecoveryEdit> edits,
                                  TextOffset resumeFloor) noexcept {
    committedScript.edits = std::move(edits);
    recoveryState.committedRecoveryEditIndex = 0;
    committedScript.resumeFloor = resumeFloor;
  }

  inline void finalizeRecoveryAtEof() noexcept {
    if (cursor() != end) {
      return;
    }
    if (recoveryState.windowReplay.inRecoveryPhase &&
        currentEditWindow() != nullptr) {
      completeActiveRecoveryWindow(true);
      return;
    }
    if (recoveryState.windowReplay.awaitingStrictStability) {
      recoveryState.windowReplay.stableAfterRecovery = true;
      recoveryState.windowReplay.awaitingStrictStability = false;
      maybeDisableRecoveryBookkeeping();
    }
  }
  [[nodiscard]] constexpr bool isInRecoveryPhase() const noexcept {
    return recoveryState.windowReplay.inRecoveryPhase;
  }
  [[nodiscard]] constexpr bool hasPendingRecoveryWindows() const noexcept {
    return editWindow.has_value() &&
           !recoveryState.windowReplay.activeEditWindowCompleted;
  }

  [[nodiscard]] constexpr TextOffset
  pendingRecoveryWindowBeginOffset() const noexcept {
    return hasPendingRecoveryWindows()
               ? std::max(editWindow->beginOffset, committedScript.resumeFloor)
               : 0;
  }
  [[nodiscard]] constexpr TextOffset
  pendingRecoveryWindowMaxCursorOffset() const noexcept {
    return hasPendingRecoveryWindows() ? editWindow->maxCursorOffset : 0;
  }
  [[nodiscard]] std::size_t activeRecoveryDepth() const noexcept {
    return _activeRecoveries.size();
  }
  [[nodiscard]] constexpr bool hasHadEdits() const noexcept {
    return recoveryState.editBudget.hadEdits;
  }
  [[nodiscard]] constexpr bool hasPendingCommittedRecoveryEdits() const
      noexcept {
    return recoveryState.committedRecoveryEditIndex <
           committedScript.edits.size();
  }
  [[nodiscard]] constexpr std::uint32_t
  completedRecoveryWindowCount() const noexcept {
    return recoveryState.windowReplay.completedRecoveryWindows;
  }
  [[nodiscard]] constexpr bool hasReachedRecoveryTarget() const noexcept {
    return recoveryState.windowReplay.reachedRecoveryTarget;
  }
  [[nodiscard]] constexpr bool isStableAfterRecovery() const noexcept {
    return recoveryState.windowReplay.stableAfterRecovery;
  }

  [[nodiscard]] bool isActiveRecovery(
      const grammar::AbstractElement *element) const noexcept {
    return _activeRecoveries.contains(cursor(), element);
  }

  [[nodiscard]] ActiveRecoveryGuard
  enterActiveRecovery(const grammar::AbstractElement *element) noexcept {
    return ActiveRecoveryGuard(_activeRecoveries, *this, element);
  }

  [[nodiscard]] EditStateGuard
  withEditPermissions(bool nextAllowInsert, bool nextAllowDelete) noexcept {
    return EditStateGuard(*this, nextAllowInsert, nextAllowDelete,
                          editPolicy.trackEditState);
  }

  /// Disable insert/delete and edit-state tracking together for the scope
  /// of the returned RAII guard. Used by recovery probes and no-edit
  /// replay attempts that must observe the strict-parse view of the
  /// context without committing edits or bumping any edit-budget
  /// counters.
  [[nodiscard]] EditStateGuard withEditTrackingDisabled() noexcept {
    return EditStateGuard(*this, false, false, false);
  }

  [[nodiscard]] constexpr bool
  allowsScopedLeadingTerminalInsertRecovery() const noexcept {
    return allowLeadingTerminalInsertScope;
  }

  [[nodiscard]] constexpr bool
  allowsCompletedWindowContinuationRecovery() const noexcept {
    return editWindow.has_value() && allowDestructiveWindowContinuation &&
           recoveryState.editBudget.hadEdits &&
           recoveryState.windowReplay.activeEditWindowCompleted &&
           !hasPendingCommittedRecoveryEdits();
  }

  /// True when no recovery descent is currently active and none is pending:
  /// the recovery-mode combinators use this to short-circuit straight into
  /// the strict parse path. Centralises the verbatim 3-term guard that
  /// Group/OrderedChoice/Repetition/ParserRule/DataTypeRule replicate.
  [[nodiscard]] constexpr bool recoveryDescentInactive() const noexcept {
    return !isInRecoveryPhase() && !hasPendingRecoveryWindows() &&
           !allowsCompletedWindowContinuationRecovery();
  }

  /// True when the current element committed observable progress relative to
  /// the supplied checkpoint: the live cursor advanced, or the edit count or
  /// edit cost grew. Captures the triple-disjunction that Group replicates
  /// around its sequence-element parse.
  [[nodiscard]] constexpr bool
  committedProgressSince(const Checkpoint &checkpoint) const noexcept {
    return cursor() != checkpoint.cursor() ||
           currentEditCount() !=
               checkpoint.recoveryState.editBudget.editCount ||
           currentEditCost() != checkpoint.recoveryState.editBudget.editCost;
  }

  [[nodiscard]] bool
  allowsCompletedWindowInsertionClusterAtCursor() const noexcept {
    return editWindow.has_value() && recoveryState.editBudget.hadEdits &&
           recoveryState.windowReplay.activeEditWindowCompleted &&
           !hasPendingCommittedRecoveryEdits() &&
           cursorOffset() >= editWindow->editFloorOffset &&
           continues_local_edit_cluster_at_cursor();
  }

  [[nodiscard]] constexpr bool canEditAtOffset(TextOffset offset) const noexcept {
    if (hasPendingCommittedRecoveryEdits()) {
      return canReplayCommittedEditAtOffset(offset);
    }
    if (!editWindow.has_value()) {
      return recoveryState.windowReplay.inRecoveryPhase &&
             offset >= editPolicy.editFloorOffset;
    }
    if (!hasPendingRecoveryWindows()) {
      return allowsCompletedWindowContinuationRecovery() &&
             offset >= editWindow->editFloorOffset;
    }
    return offset >= editWindow->editFloorOffset;
  }

  [[nodiscard]] constexpr bool canEdit() const noexcept {
    return canEditAtOffset(cursorOffset());
  }


  [[nodiscard]] constexpr bool canInsert() const noexcept {
    return detail::can_insert(editPolicy.allowInsert, canEdit());
  }

  /// Shared guard for replace/delete edits: refuse a destructive edit that
  /// lands past the active recovery window's max cursor once the window has
  /// already made edits, unless the edit continues the current local edit
  /// cluster or the caller opens `windowContinuationHatch` (a per-edit-kind
  /// escape: a single-edit fuzzy keyword replace, or the first delete of a
  /// run).
  [[nodiscard]] constexpr bool
  destructiveEditOutsideActiveWindow(bool windowContinuationHatch) const noexcept {
    const auto *window = currentEditWindow();
    return recoveryState.windowReplay.inRecoveryPhase && window != nullptr &&
           cursorOffset() > window->maxCursorOffset &&
           recoveryState.editBudget.hadEdits &&
           !allowDestructiveWindowContinuation &&
           !continues_local_edit_cluster_at_cursor() && windowContinuationHatch;
  }

  [[nodiscard]] constexpr bool canDelete() const noexcept {
    if (_cursor >= end) {
      return false;
    }
    // A pending committed delete at the cursor must remain replayable even
    // when the active window's editFloor would otherwise refuse new edits:
    // committed edits were already accepted by an earlier window and replay
    // them is not a fresh edit. Without this every recovery decision point
    // upstream of `deleteOneCodepoint` (legality checks, scans, family
    // selection) would refuse to engage at offsets below editFloor, and the
    // committed prefix could not be threaded across windows.
    if (matching_committed_delete(cursorOffset()) != nullptr) {
      return true;
    }
    return detail::can_delete(editPolicy.allowDelete, canEdit(),
                              recoveryState.editBudget.consecutiveDeletes,
                              editPolicy.maxConsecutiveCodepointDeletes,
                              *cursor());
  }

  [[nodiscard]] constexpr bool
  isWordCharAt(const char *p, const char *codepointEnd) const noexcept {
    return detail::is_word_char_at(wordChars, p, codepointEnd);
  }

  [[nodiscard]] constexpr std::uint32_t currentEditCost() const noexcept {
    return recoveryState.editBudget.editCost;
  }
  [[nodiscard]] constexpr std::uint32_t currentEditCount() const noexcept {
    return recoveryState.editBudget.editCount;
  }
  [[nodiscard]] constexpr std::uint32_t
  currentWindowEditCost() const noexcept {
    return recoveryState.editBudget.editCost -
           recoveryState.windowReplay.activeWindowEditCostBase;
  }
  [[nodiscard]] constexpr std::uint32_t
  currentWindowEditCount() const noexcept {
    return recoveryState.editBudget.editCount -
           recoveryState.windowReplay.activeWindowEditCountBase;
  }

  [[nodiscard]] constexpr std::uint32_t
  editCostDelta(const Checkpoint &checkpoint) const noexcept {
    return recoveryState.editBudget.editCost -
           checkpoint.recoveryState.editBudget.editCost;
  }

  [[nodiscard]] std::size_t recoveryEditCount() const noexcept {
    return recoveryEdits.size();
  }

  [[nodiscard]] std::span<const RecoveryEdit> recoveryEditsView() const
      noexcept {
    return recoveryEdits;
  }

  [[nodiscard]] std::vector<RecoveryEdit> snapshotRecoveryEdits() const {
    return recoveryEdits;
  }

  [[nodiscard]] std::vector<RecoveryEdit> takeRecoveryEdits() noexcept {
    return std::move(recoveryEdits);
  }

  [[nodiscard]] std::optional<std::uint32_t>
  replayForwardTokenCount() const {
    if (!editWindow.has_value()) {
      return std::nullopt;
    }
    return active_window_forward_token_budget(*editWindow);
  }

  [[nodiscard]] constexpr bool
  canAffordEdit(ParseDiagnosticKind kind) const noexcept {
    return canAffordEditImpl(kind);
  }
  [[nodiscard]] constexpr bool
  canAffordEdit(std::uint32_t customCost) const noexcept {
    return canAffordEditImpl(customCost);
  }

  inline void allowBudgetOverflowEdits() noexcept {
    if (!recoveryState.editBudget.allowBudgetOverflowEdits) {
      recoveryState.editBudget.allowBudgetOverflowEdits = true;
    }
  }

  bool insertSynthetic(const grammar::AbstractElement *element);

  bool insertSyntheticGapAt(const char *position,
                            const char *message = nullptr);

  bool deleteOneCodepoint() noexcept;

  bool extendLastDeleteThroughHiddenTrivia() noexcept;

  bool replaceLeaf(const char *endPtr, const grammar::AbstractElement *element,
                   std::uint32_t replacementCost, bool hidden = false,
                   std::uint32_t fuzzyOperationDistance =
                       std::numeric_limits<std::uint32_t>::max()) {
    if (!editPolicy.trackEditState) {
      return false;
    }
    clearPendingDeleteHiddenTriviaBridge();
    if (endPtr <= cursor() || endPtr > end) {
      PEGIUM_RECOVERY_TRACE("[rule] replace blocked offset=", cursorOffset(),
                            " floor=", editPolicy.editFloorOffset);
      return false;
    }
    const auto endOffset = static_cast<TextOffset>(endPtr - begin);
    const bool replayingCommittedReplace =
        matches_committed_replace(cursorOffset(), endOffset, element);
    if (hasPendingCommittedRecoveryEdits() && !replayingCommittedReplace) {
      PEGIUM_RECOVERY_TRACE(
          "[rule] replace blocked pending committed offset=", cursorOffset(),
          " floor=", editPolicy.editFloorOffset);
      return false;
    }
    // Carve-out for the canonical "truncated/typoed word keyword" Replace.
    // We allow such a Replace to bypass the destructive-outside-window
    // guard because cascading recoveries (e.g. `statemachin` +
    // `initialStat` both typoed) otherwise hit the wall: the first commit
    // moves the active window past the second typo, so the second Replace
    // falls back to a synthetic Insert at a higher rule level which
    // leaves the typoed token unconsumed.
    //
    // Thresholds: a SINGLE fuzzy edit operation (Damerau-Levenshtein
    // operation distance == 1, e.g. one substitution / transposition / extra
    // or missing codepoint), and the target literal must be a word (≥ 3
    // codepoints). Gating on the operation distance rather than the weighted
    // `replacementCost` admits single-operation repairs whose weighted cost is
    // > 1 (a transposition or extra-codepoint Replace costs 2), while still
    // keeping the symbolic-literal guards in place via the word-length floor —
    // `>` → `=>` (literal length 2) stays gated, `re` → `req` (length 3) and
    // `initialStat` → `initialState` (length 12) go through.
    //
    // `forbidOutOfWindowFuzzyFold` (mirror of
    // `RecoveryAttemptSpec::probeAxes.forbidOutOfWindowFuzzyFold`) forces the
    // carve-out OFF for the no-fold sibling probe, which re-descends the same
    // window to expose a competing pure-delete candidate to the ranker.
    constexpr std::size_t kWordLiteralLengthFloor = 3u;
    const TextOffset replaceSpan =
        endOffset > cursorOffset() ? endOffset - cursorOffset() : 0;
    const auto *literalElement =
        element != nullptr &&
                element->getKind() == grammar::ElementKind::Literal
            ? static_cast<const grammar::Literal *>(element)
            : nullptr;
    const bool targetsWordKeyword =
        literalElement != nullptr &&
        literalElement->getValue().size() >= kWordLiteralLengthFloor;
    const bool singleEditFuzzyKeywordReplace =
        !forbidOutOfWindowFuzzyFold && targetsWordKeyword &&
        fuzzyOperationDistance == 1u && replaceSpan > 0;
    if (!canEdit() ||
        destructiveEditOutsideActiveWindow(!singleEditFuzzyKeywordReplace) ||
        !canEditAtOffset(endOffset) ||
        !canAffordEdit(replacementCost)) {
      PEGIUM_RECOVERY_TRACE("[rule] replace blocked offset=", cursorOffset(),
                            " floor=", editPolicy.editFloorOffset);
      return false;
    }
    const auto beforeOffset = cursorOffset();
    (void)beforeOffset;
    recoveryEdits.push_back({.kind = ParseDiagnosticKind::Replaced,
                             .offset = cursorOffset(),
                             .beginOffset = cursorOffset(),
                             .endOffset = endOffset,
                             .element = element});
    detail::apply_non_delete_edit_state(
        replacementCost, recoveryState.editBudget.editCost,
        recoveryState.editBudget.editCount,
        recoveryState.editBudget.hadEdits,
        recoveryState.editBudget.consecutiveDeletes);
    if (replayingCommittedReplace) {
      consumeCommittedRecoveryEdit();
    }
    noteReplayForwardRequirementForCurrentWindow(ParseDiagnosticKind::Replaced);
    PEGIUM_STEP_TRACE_INC(detail::StepCounter::ParseContextReplace);
    leaf(endPtr, element, hidden, true);
    PEGIUM_RECOVERY_TRACE("[rule] replace offset=", beforeOffset, " -> ",
                          cursorOffset(),
                          " kind=", static_cast<int>(element->getKind()));
    return true;
  }

private:
  friend struct TrackedParseContext;

  /// Shared body for both `canAffordEdit` overloads. The only difference
  /// between the kind- and cost-based forms is the final argument forwarded
  /// to the overloaded `detail::can_afford_edit`; everything else (the
  /// budget-overflow early-out, the window-scoped vs global counter
  /// selection) is identical.
  template <class Cost>
  [[nodiscard]] constexpr bool canAffordEditImpl(Cost cost) const noexcept {
    if (recoveryState.editBudget.allowBudgetOverflowEdits) {
      return true;
    }
    if (recoveryState.windowReplay.inRecoveryPhase &&
        currentEditWindow() != nullptr) {
      return detail::can_afford_edit(
          currentWindowEditCount(), currentWindowEditCost(),
          editPolicy.maxEditsPerAttempt, editPolicy.maxEditCost, cost);
    }
    return detail::can_afford_edit(
        recoveryState.editBudget.editCount, recoveryState.editBudget.editCost,
        editPolicy.maxEditsPerAttempt, editPolicy.maxEditCost, cost);
  }

  inline void afterTrackedSkip() noexcept {
    clearPendingDeleteHiddenTriviaBridge();
    refreshRecoveryPhase();
  }

  inline void afterTrackedLeaf(TextOffset beginOffset, TextOffset endOffset,
                               bool hidden) noexcept {
    clearPendingDeleteHiddenTriviaBridge();
    if (recoveryState.windowReplay.recoveryBookkeepingEnabled && !hidden &&
        endOffset > beginOffset) {
      onVisibleLeaf(beginOffset, endOffset);
    }
    refreshRecoveryPhase();
    if (editPolicy.trackEditState &&
        recoveryState.editBudget.consecutiveDeletes != 0) {
      recoveryState.editBudget.consecutiveDeletes = 0;
    }
  }

  [[nodiscard]] constexpr const EditWindow *currentEditWindow() const noexcept {
    return hasPendingRecoveryWindows() ? &*editWindow : nullptr;
  }

  [[nodiscard]] constexpr EditWindow *currentEditWindowMutable() noexcept {
    return hasPendingRecoveryWindows() ? &*editWindow : nullptr;
  }

  [[nodiscard]] constexpr const RecoveryEdit *nextCommittedRecoveryEdit() const
      noexcept {
    return hasPendingCommittedRecoveryEdits()
               ? std::addressof(
                     committedScript
                         .edits[recoveryState.committedRecoveryEditIndex])
               : nullptr;
  }

  [[nodiscard]] constexpr bool
  canReplayCommittedEditAtOffset(TextOffset offset) const noexcept {
    const auto *entry = nextCommittedRecoveryEdit();
    if (entry == nullptr) {
      return false;
    }
    using enum ParseDiagnosticKind;
    switch (entry->kind) {
    case Inserted:
      return entry->beginOffset == offset && entry->endOffset == offset;
    case Deleted:
      // A committed Delete at [b, e) may replay from any cursor position
      // c < e: cursors c < b consume the gap between the prior edit and the
      // original delete span, producing a single merged Delete@[c, e) that
      // matches the behavior of the original accepted attempt.
      // (Permitting c < b is intentional and load-bearing for the
      // delete-merge replay path; the strict `matching_committed_delete`
      // predicate guards the actual replay site.)
      return offset < entry->endOffset;
    case Replaced:
      // The upper bound is INCLUSIVE (`<= endOffset`) and load-bearing: a
      // committed Replace replay validates its span END via
      // canEditAtOffset(endOffset) at replaceLeaf, so the end position itself
      // must be replayable. An exclusive `<` would reject the legal replay and
      // break the cross-window Replace cascade (e.g. statemachine/initialStat).
      return offset >= entry->beginOffset && offset <= entry->endOffset;
    case Incomplete:
    case Recovered:
    case ConversionError:
      return false;
    }
    return false;
  }

  [[nodiscard]] constexpr bool
  matches_committed_insert(TextOffset offset,
                           const grammar::AbstractElement *element,
                           std::string_view message) const noexcept {
    const auto *entry = nextCommittedRecoveryEdit();
    return entry != nullptr && entry->kind == ParseDiagnosticKind::Inserted &&
           entry->beginOffset == offset && entry->endOffset == offset &&
           elements_equivalent_for_replay(entry->element, element) &&
           entry->message == message;
  }

  [[nodiscard]] constexpr const RecoveryEdit *
  matching_committed_delete(TextOffset offset) const noexcept {
    const auto *entry = nextCommittedRecoveryEdit();
    return entry != nullptr && entry->kind == ParseDiagnosticKind::Deleted &&
                   offset >= entry->beginOffset && offset < entry->endOffset
               ? entry
               : nullptr;
  }

  [[nodiscard]] constexpr bool
  matches_committed_replace(TextOffset beginOffset, TextOffset endOffset,
                            const grammar::AbstractElement *element) const
      noexcept {
    const auto *entry = nextCommittedRecoveryEdit();
    return entry != nullptr && entry->kind == ParseDiagnosticKind::Replaced &&
           entry->beginOffset == beginOffset && entry->endOffset == endOffset &&
           elements_equivalent_for_replay(entry->element, element);
  }

  inline void consumeCommittedRecoveryEdit() noexcept {
    if (!hasPendingCommittedRecoveryEdits()) {
      return;
    }
    ++recoveryState.committedRecoveryEditIndex;
  }

  inline void advanceCommittedDeleteReplay() noexcept {
    const auto *entry = nextCommittedRecoveryEdit();
    if (entry == nullptr || entry->kind != ParseDiagnosticKind::Deleted) {
      return;
    }
    if (cursorOffset() >= entry->endOffset) {
      consumeCommittedRecoveryEdit();
    }
  }

  [[nodiscard]] inline bool
  continues_local_edit_cluster_at_cursor() const noexcept {
    if (recoveryEdits.empty()) {
      return false;
    }
    const auto cursorOffsetValue = cursorOffset();
    const auto &lastEdit = recoveryEdits.back();
    return lastEdit.beginOffset == cursorOffsetValue &&
           lastEdit.endOffset == cursorOffsetValue;
  }

  [[nodiscard]] static constexpr std::uint32_t
  active_window_forward_token_budget(const EditWindow &window) noexcept {
    return std::max(window.forwardTokenCount, window.replayForwardTokenCount);
  }

  inline void
  noteReplayForwardRequirementForCurrentWindow(ParseDiagnosticKind editKind) noexcept {
    if (!recoveryState.windowReplay.inRecoveryPhase) {
      return;
    }
    auto *window = currentEditWindowMutable();
    if (window == nullptr) {
      return;
    }
    const auto replayExtensionBase =
        editKind == ParseDiagnosticKind::Inserted
            ? stabilityTokenCount
            : window->forwardTokenCount;
    const auto extendedForwardTokenCount =
        static_cast<std::uint64_t>(
            recoveryState.windowReplay.currentForwardVisibleLeafCount) +
        static_cast<std::uint64_t>(replayExtensionBase);
    const auto nextReplayForwardTokenCount =
        static_cast<std::uint32_t>(
            std::min<std::uint64_t>(extendedForwardTokenCount,
                                    std::numeric_limits<std::uint32_t>::max()));
    if (nextReplayForwardTokenCount <= window->replayForwardTokenCount) {
      return;
    }
    window->replayForwardTokenCount = nextReplayForwardTokenCount;
  }

  [[nodiscard]] inline RecoveryEdit *pendingHiddenTriviaDeleteEdit() noexcept {
    const auto &deleteBridge = recoveryState.deleteBridge;
    if (deleteBridge.pendingHiddenTriviaStart == nullptr ||
        deleteBridge.pendingHiddenTriviaEnd == nullptr) {
      return nullptr;
    }
    if (deleteBridge.pendingHiddenTriviaEnd != cursor() ||
        recoveryEdits.empty()) {
      clearPendingDeleteHiddenTriviaBridge();
      return nullptr;
    }
    auto &lastEdit = recoveryEdits.back();
    if (lastEdit.kind != ParseDiagnosticKind::Deleted ||
        lastEdit.endOffset !=
            static_cast<TextOffset>(deleteBridge.pendingHiddenTriviaStart -
                                    begin)) {
      clearPendingDeleteHiddenTriviaBridge();
      return nullptr;
    }
    return &lastEdit;
  }

  inline void clearPendingDeleteHiddenTriviaBridge() noexcept {
    if (recoveryState.deleteBridge.pendingHiddenTriviaStart == nullptr &&
        recoveryState.deleteBridge.pendingHiddenTriviaEnd == nullptr) {
      return;
    }
    recoveryState.deleteBridge = {};
  }

  inline void beginActiveRecoveryWindow() noexcept {
    recoveryState.windowReplay.recoveryBookkeepingEnabled = true;
    _recordFailureHistory = true;
    recoveryState.windowReplay.inRecoveryPhase = true;
    recoveryState.windowReplay.activeWindowEditCostBase =
        recoveryState.editBudget.editCost;
    recoveryState.windowReplay.activeWindowEditCountBase =
        recoveryState.editBudget.editCount;
    recoveryState.windowReplay.currentForwardVisibleLeafCount = 0;
    recoveryState.windowReplay.strictVisibleLeafCountAfterRecovery = 0;
    recoveryState.windowReplay.reachedRecoveryTarget = false;
    recoveryState.windowReplay.stableAfterRecovery = false;
    recoveryState.windowReplay.awaitingStrictStability = false;
    editPolicy.editFloorOffset = editWindow->editFloorOffset;
  }

  inline void completeActiveRecoveryWindow(bool reachedEof,
                                           bool handedOffToNextWindow = false) noexcept {
    ++recoveryState.windowReplay.completedRecoveryWindows;
    recoveryState.windowReplay.reachedRecoveryTarget =
        handedOffToNextWindow || reachedEof ||
        recoveryState.windowReplay.currentForwardVisibleLeafCount >=
            active_window_forward_token_budget(*editWindow);
    recoveryState.windowReplay.stableAfterRecovery = reachedEof;
    recoveryState.windowReplay.awaitingStrictStability =
        recoveryState.windowReplay.reachedRecoveryTarget &&
        !handedOffToNextWindow &&
        !recoveryState.windowReplay.stableAfterRecovery;
    recoveryState.windowReplay.strictVisibleLeafCountAfterRecovery = 0;
    recoveryState.windowReplay.currentForwardVisibleLeafCount = 0;
    recoveryState.windowReplay.inRecoveryPhase = false;
    recoveryState.windowReplay.activeEditWindowCompleted = true;
    maybeDisableRecoveryBookkeeping();
  }

  inline void onVisibleLeaf(TextOffset beginOffset, TextOffset endOffset) noexcept {
    if (endOffset <= beginOffset) {
      return;
    }
    if (recoveryState.windowReplay.inRecoveryPhase) {
      if (!recoveryState.editBudget.hadEdits) {
        return;
      }
      if (const auto *window = currentEditWindow();
          window != nullptr && beginOffset >= window->maxCursorOffset) {
        ++recoveryState.windowReplay.currentForwardVisibleLeafCount;
      }
      return;
    }
    if (recoveryState.windowReplay.awaitingStrictStability) {
      ++recoveryState.windowReplay.strictVisibleLeafCountAfterRecovery;
      if (recoveryState.windowReplay.strictVisibleLeafCountAfterRecovery >=
          stabilityTokenCount) {
        recoveryState.windowReplay.stableAfterRecovery = true;
        recoveryState.windowReplay.awaitingStrictStability = false;
        maybeDisableRecoveryBookkeeping();
        return;
      }
    }
  }

  inline void maybeDisableRecoveryBookkeeping() noexcept {
    if (recoveryState.windowReplay.inRecoveryPhase ||
        recoveryState.windowReplay.awaitingStrictStability ||
        hasPendingRecoveryWindows()) {
      return;
    }
    recoveryState.windowReplay.recoveryBookkeepingEnabled = false;
  }

  detail::ActiveRecoveryStack _activeRecoveries;
};

// Out-of-line bodies of `TrackedParseContext::skip`/`leaf`: they downcast to
// `RecoveryContext` (`_runRecoveryBookkeeping` gates the downcast, so a
// failure-only `TrackedParseContext` never takes it), so they must be defined
// after `RecoveryContext`'s complete type is visible.

inline void TrackedParseContext::skip() noexcept {
  const char *const before = _cursor;
  ParseContext::skip();
  if (_cursor > _maxCursor) [[likely]] {
    _maxCursor = _cursor;
  }
  if (_recordFailureHistory) [[likely]] {
    _failureRecorder.onCursor(cursor());
  }
  if (_runRecoveryBookkeeping) [[likely]] {
    // The two skipper variants converge to the same end for a given begin;
    // populate the no-builder cache so subsequent recovery probes at this
    // position skip the underlying terminal scan. Gated on
    // `_runRecoveryBookkeeping` because `skip_without_builder()` is called
    // exclusively from recovery code paths — the failure-only TrackedParseContext
    // never reads this cache.
    _skipCacheBegin = before;
    _skipCacheEnd = _cursor;
    _skipCacheSkipper = _skipper;
    static_cast<RecoveryContext &>(*this).afterTrackedSkip();
  }
}

inline void TrackedParseContext::leaf(
    const char *endPtr, const grammar::AbstractElement *element, bool hidden,
    bool recovered) {
  const auto beginOffset = cursorOffset();
  const auto endOffset = static_cast<TextOffset>(endPtr - begin);
  if (_recordFailureHistory) [[likely]] {
    _failureRecorder.onLeaf(cursor(), endPtr, element, hidden);
  }
  ParseContext::leaf(endPtr, element, hidden, recovered);
  if (_cursor > _maxCursor) [[likely]] {
    _maxCursor = _cursor;
  }
  if (_runRecoveryBookkeeping) [[likely]] {
    static_cast<RecoveryContext &>(*this).afterTrackedLeaf(beginOffset,
                                                           endOffset, hidden);
  }
}

inline void TrackedParseContext::leaf(
    const char *beginPtr, const char *endPtr,
    const grammar::AbstractElement *element, bool hidden, bool recovered) {
  const auto beginOffset = static_cast<TextOffset>(beginPtr - begin);
  const auto endOffset = static_cast<TextOffset>(endPtr - begin);
  if (_recordFailureHistory) [[likely]] {
    _failureRecorder.onLeaf(beginPtr, endPtr, element, hidden);
  }
  ParseContext::leaf(beginPtr, endPtr, element, hidden, recovered);
  if (_cursor > _maxCursor) [[likely]] {
    _maxCursor = _cursor;
  }
  if (_runRecoveryBookkeeping) [[likely]] {
    static_cast<RecoveryContext &>(*this).afterTrackedLeaf(beginOffset,
                                                           endOffset, hidden);
  }
}

} // namespace pegium::parser
