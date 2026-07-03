#include <pegium/core/parser/RecoverySearch.hpp>

#include <algorithm>
#include <limits>
#include <optional>
#include <string_view>

#include <pegium/core/grammar/Assignment.hpp>
#include <pegium/core/grammar/DataTypeRule.hpp>
#include <pegium/core/grammar/Group.hpp>
#include <pegium/core/grammar/InfixRule.hpp>
#include <pegium/core/grammar/OrderedChoice.hpp>
#include <pegium/core/grammar/Repetition.hpp>
#include <pegium/core/grammar/UnorderedGroup.hpp>
#include <pegium/core/parser/EditableRecoverySupport.hpp>
#include <pegium/core/parser/RecoveryContext.hpp>
#include <pegium/core/parser/RecoveryCandidate.hpp>
#include <pegium/core/parser/RecoveryDebug.hpp>
#include <pegium/core/parser/RecoveryUtils.hpp>
#include <pegium/core/parser/StepTrace.hpp>
#include <pegium/core/syntax-tree/CstBuilder.hpp>

namespace pegium::parser::detail {

/// Recovery caches reused across every re-parse of one recovery search. The
/// choice cache is cleared before each probe (so behaviour is identical to a
/// per-attempt cache, only the ~1.4 MB allocation is reused); the fuzzy cache is
/// shared warm because its entries are a pure function of (literal, input,
/// case-sensitivity) and `store` frees on overwrite, so reuse only adds hits.
struct RecoveryParseCachePool {
  ChoiceRecoverCache choice;
  LiteralFuzzyCandidatesCache fuzzy;
};

AttemptFacts
derive_attempt_facts(const RecoveryAttempt &attempt) noexcept {
  AttemptFacts facts;
  facts.hasEdits = !attempt.recoveryEdits.empty();
  if (!facts.hasEdits) {
    return facts;
  }

  // Pass 1 — aggregate per-edit data via the shared edit-slice summary.
  // Recovery edits are pushed in offset order, so the first edit's begin
  // offset is the minimum and the running max captures the last end offset.
  const auto editSummary =
      summarize_edits_since(attempt.recoveryEdits, 0);
  const auto firstEditOffset = editSummary.firstEditBeginOffset;
  facts.lastEditOffset = editSummary.maxEndOffset;
  facts.hasDeleteOnlyRecovery = editSummary.allDeleted;

  // Pass 2 — derive composite booleans from aggregates + attempt fields.
  facts.continuesAfterFirstEdit =
      attempt.parsedLength > facts.lastEditOffset;
  facts.hasCursorProgressPastLastEdit =
      attempt.maxCursorOffset > facts.lastEditOffset;

  facts.preservesStablePrefixBeforeFirstEdit =
      attempt.hasStablePrefix && firstEditOffset >= attempt.stablePrefixOffset;

  facts.hasEditPastReplayWindowHorizon =
      attempt.replayWindow.has_value() &&
      facts.lastEditOffset > attempt.replayWindow->maxCursorOffset;

  return facts;
}

#if defined(PEGIUM_ENABLE_RECOVERY_TRACE)
void trace_recovery_json(const char *label, const pegium::JsonValue &value) {
  PEGIUM_RECOVERY_TRACE(label, " ", value.toJsonString({.pretty = false}));
}

void trace_strict_summary(const StrictParseSummary &summary) {
  trace_recovery_json("[parser strict]", strict_parse_summary_to_json(summary));
}

void trace_failure_snapshot(const FailureSnapshot &snapshot) {
  trace_recovery_json("[parser snapshot]", failure_snapshot_to_json(snapshot));
}

void trace_recovery_window(std::uint32_t windowIndex, std::uint32_t tokenCount,
                           const RecoveryWindow &window,
                           std::size_t selectedWindowCount) {
  auto payload = recovery_window_to_json(window);
  auto &object = payload.object();
  object["selectedWindowCount"] =
      static_cast<std::int64_t>(selectedWindowCount);
  object["windowIndex"] = static_cast<std::int64_t>(windowIndex);
  object["requestedTokenCount"] = static_cast<std::int64_t>(tokenCount);
  trace_recovery_json("[parser window]", payload);
}

void trace_recovery_attempt(const RecoveryAttempt &attempt,
                            const RecoveryAttemptSpec &spec) {
  trace_recovery_json("[parser attempt]",
                      recovery_attempt_to_json(attempt, &spec));
}
#else
// With recovery tracing disabled, these helpers are unused. Keeping them
// behind the macro avoids building JsonValue payloads (and pulling
// `RecoveryDebug.cpp`'s formatter into the hot path) when tracing is off,
// which is the case for release and the standard test build.
inline void trace_recovery_json(const char *,
                                const pegium::JsonValue &) noexcept { /* tracing disabled */
}
inline void trace_strict_summary(const StrictParseSummary &) noexcept { /* tracing disabled */
}
inline void trace_failure_snapshot(const FailureSnapshot &) noexcept { /* tracing disabled */
}
inline void trace_recovery_window(std::uint32_t, std::uint32_t,
                                  const RecoveryWindow &,
                                  std::size_t) noexcept { /* tracing disabled */ }
inline void trace_recovery_attempt(const RecoveryAttempt &,
                                   const RecoveryAttemptSpec &) noexcept { /* tracing disabled */
}
#endif

struct RecoveryWindowPrefixContract {
  TextOffset editFloorOffset = 0;
  TextOffset stablePrefixOffset = 0;
  bool hasStablePrefix = false;
};

struct CommittedPrefixContract {
  TextOffset resumeFloor = 0;
  std::size_t preservedEditCount = 0u;
};

struct StrictFailureStageResult {
  RecoveryAttempt strictAttempt;
  std::optional<FailureSnapshot> failureSnapshot;
  TextOffset failureVisibleCursorOffset = 0;
};

namespace {

[[nodiscard]] std::optional<RecoveryAttempt> consider_window_attempt_candidate(
    RecoveryAttempt attempt, const RecoveryAttempt &selectedAttempt,
    bool continuingRecovery);

[[nodiscard]] CommittedPrefixContract
committed_replay_prefix_contract(
    const RecoveryAttempt &selectedAttempt) noexcept;

[[nodiscard]] constexpr bool
same_syntax_script_entry(const SyntaxScriptEntry &lhs,
                         const SyntaxScriptEntry &rhs) noexcept;

[[nodiscard]] TextOffset
visible_leaf_stable_prefix_offset(const FailureSnapshot &snapshot) noexcept {
  const auto stableLeafEnd = snapshot.hasFailureToken
                                 ? snapshot.failureTokenIndex
                                 : snapshot.failureLeafHistory.size();
  TextOffset stablePrefixOffset = 0;
  for (std::size_t index = 0; index < stableLeafEnd; ++index) {
    const auto &leaf = snapshot.failureLeafHistory[index];
    if (leaf.endOffset > snapshot.maxCursorOffset) {
      break;
    }
    stablePrefixOffset = leaf.endOffset;
  }
  return stablePrefixOffset;
}

[[nodiscard]] RecoveryWindowPrefixContract recovery_window_prefix_contract(
    const FailureSnapshot &snapshot,
    const RecoveryAttempt &selectedAttempt) noexcept {
  if (selectedAttempt.parsedLength != 0) {
    return {.editFloorOffset = selectedAttempt.parsedLength,
            .stablePrefixOffset = selectedAttempt.parsedLength,
            .hasStablePrefix = true};
  }
  if (const auto stablePrefixOffset =
          visible_leaf_stable_prefix_offset(snapshot);
      stablePrefixOffset != 0) {
    return {.editFloorOffset = stablePrefixOffset,
            .stablePrefixOffset = stablePrefixOffset,
            .hasStablePrefix = true};
  }
  if (!snapshot.hasFailureToken) {
    return {};
  }
  return {
      .editFloorOffset =
          snapshot.failureLeafHistory[snapshot.failureTokenIndex].beginOffset,
      .stablePrefixOffset = 0,
      .hasStablePrefix = false};
}

[[nodiscard]] TextOffset
failure_visible_cursor_offset(const FailureSnapshot &snapshot,
                              TextOffset fallbackOffset) noexcept {
  if (snapshot.hasFailureToken) {
    return snapshot.failureLeafHistory[snapshot.failureTokenIndex].endOffset;
  }
  if (!snapshot.failureLeafHistory.empty()) {
    return snapshot.failureLeafHistory.back().endOffset;
  }
  return fallbackOffset;
}

void fill_strict_attempt_from_summary(RecoveryAttempt &attempt,
                                      const StrictParseSummary &summary) noexcept {
  attempt.entryRuleMatched = summary.entryRuleMatched;
  attempt.parsedLength = summary.parsedLength;
  attempt.lastVisibleCursorOffset = summary.lastVisibleCursorOffset;
  attempt.fullMatch = summary.fullMatch;
  attempt.maxCursorOffset = summary.maxCursorOffset;
}

[[nodiscard]] StrictFailureStageResult
parse_strict_capturing_failure(const grammar::ParserRule &entryRule,
                         const Skipper &skipper, const ParseOptions &options,
                         const text::TextSnapshot &text,
                         const utils::CancellationToken &cancelToken) {
  StrictFailureStageResult result;

  // Fast path: run strict parse without failure history.
  //
  // On fully-valid inputs this is the only pass we need; the failure history
  // recorder is only required when recovery has to reconstruct where the
  // parse derailed. Paying its per-leaf bookkeeping on every leaf of every
  // successful parse is wasteful, so we first try a bare `ParseContext` and
  // only re-run with a `TrackedParseContext` if the strict parse did not
  // reach `fullMatch`.
  auto strictResult =
      run_strict_parse(entryRule, skipper, text, cancelToken);
  result.strictAttempt.cst = std::move(strictResult.cst);
  fill_strict_attempt_from_summary(result.strictAttempt, strictResult.summary);
  result.failureVisibleCursorOffset =
      result.strictAttempt.lastVisibleCursorOffset;

  if (!options.recoveryEnabled || result.strictAttempt.fullMatch) {
    return result;
  }

  // Slow path: re-run with failure history to build the recovery snapshot.
  auto analysis =
      run_strict_parse_with_failure_snapshot(entryRule, skipper, text, cancelToken);
  result.strictAttempt.cst = std::move(analysis.strictResult.cst);
  const auto &summary = analysis.strictResult.summary;
  fill_strict_attempt_from_summary(result.strictAttempt, summary);
  result.failureVisibleCursorOffset =
      result.strictAttempt.lastVisibleCursorOffset;

  if (result.strictAttempt.fullMatch) {
    return result;
  }

  trace_strict_summary(summary);
  result.failureVisibleCursorOffset = failure_visible_cursor_offset(
      analysis.snapshot, result.failureVisibleCursorOffset);
  trace_failure_snapshot(analysis.snapshot);
  result.failureSnapshot = std::move(analysis.snapshot);
  return result;
}

[[nodiscard]] bool
recovery_attempt_budget_exhausted(const RecoverySearchRunResult &result,
                                  const ParseOptions &options) noexcept {
  return result.recoveryAttemptRuns >=
         std::min(options.maxRecoveryAttempts,
                  options.maxTotalRecoveryAttemptRuns);
}

[[nodiscard]] TextOffset
last_failure_leaf_end_offset(const FailureSnapshot &snapshot) noexcept {
  return snapshot.failureLeafHistory.empty()
             ? 0
             : snapshot.failureLeafHistory.back().endOffset;
}

/// Edit pruning post-hoc.
///
/// Greedy exploration inside a single recovery attempt can commit to more
/// edits than strictly needed. For example, inserting the leading `(` of a
/// `FunctionCall`'s `option("(" Args ")")` opens an opportunistic path that
/// then needs 4 more edits to fabricate the call; the same input often has
/// a 1-edit `delete` that also reaches `fullMatch` but is not surfaced as a
/// separate attempt.
///
/// After a `fullMatch` attempt, drop each edit in turn and re-run with the
/// remaining edits pinned as the committed prefix. If the parse still
/// reaches `fullMatch` with a strictly smaller edit count, keep the
/// smaller set. Iterate until stable. O(N²) per accepted attempt in the
/// worst case, bounded by `maxRecoveryEditsPerAttempt`.
///
/// Pruning probes are post-hoc and do not count against
/// `recoveryAttemptRuns`: they fire only
/// after an attempt already reached `fullMatch`, i.e. the global budget
/// was already paid to obtain that candidate. Adding them to the counter
/// would defeat tests that cap the budget at 1 attempt.
[[nodiscard]] RecoveryAttempt
minimize_recovery_edits(const grammar::ParserRule &entryRule,
                        const Skipper &skipper, const ParseOptions &options,
                        const text::TextSnapshot &text,
                        const RecoveryAttemptSpec &spec,
                        RecoveryAttempt attempt,
                        std::uint64_t &choiceRecoverCacheHits,
                        std::uint64_t &choiceRecoverCacheMisses,
                        const utils::CancellationToken &cancelToken,
                        RecoveryParseCachePool *cachePool) {
  if (!attempt.fullMatch || attempt.recoveryEdits.size() <= 1u) {
    return attempt;
  }
  // Keep post-hoc pruning a small-script compensation. Large edit sets usually
  // represent panic/resync or repeated local repairs; replaying the whole file
  // once per edit turns recovery into an O(N^2) global search and duplicates
  // work the local enumerators should own.
  constexpr std::size_t kMaxPostHocPruningEditCount = 4u;
  if (attempt.recoveryEdits.size() > kMaxPostHocPruningEditCount) {
    return attempt;
  }
  PEGIUM_STEP_TRACE_INC(StepCounter::MinimizeRecoveryEditsRuns);
  const auto editCountBeforePruning = attempt.recoveryEdits.size();

  // Keep pruning until no edit can be dropped while preserving a full match.
  // Each outer iteration restarts the scan from the first edit because dropping
  // one edit can unlock another whose necessity depended on the earlier one.
  const auto tryPrunedEditSet =
      [&](std::size_t dropIndex) -> std::optional<RecoveryAttempt> {
    RecoveryAttemptSpec prunedSpec;
    prunedSpec.window = spec.window;
    prunedSpec.committedRecoveryResumeFloor = spec.committedRecoveryResumeFloor;
    prunedSpec.committedRecoveryEdits.reserve(attempt.recoveryEdits.size());
    for (std::size_t i = 0; i < attempt.recoveryEdits.size(); ++i) {
      if (i != dropIndex) {
        prunedSpec.committedRecoveryEdits.push_back(attempt.recoveryEdits[i]);
      }
    }
    if (prunedSpec.committedRecoveryEdits.size() >=
        attempt.recoveryEdits.size()) {
      return std::nullopt;
    }

    // Run the probe forbidding any *new* edit. Committed edits in the spec are
    // still replayed; the no-new-edits invariant is enforced purely by pinning
    // `maxRecoveryEditsPerAttempt` to the committed-prefix size (not by a
    // recovery-off switch — `execute_recovery_parse` does not consult
    // `recoveryEnabled`). If the parse still reaches `fullMatch`, the dropped
    // edit was genuinely redundant.
    ParseOptions probeOptions = options;
    probeOptions.maxRecoveryEditsPerAttempt =
        static_cast<std::uint32_t>(prunedSpec.committedRecoveryEdits.size());
    auto prunedAttempt = execute_recovery_parse(
        entryRule, skipper, probeOptions, text, prunedSpec, cancelToken,
        cachePool);
    classify_recovery_attempt(prunedAttempt);
    choiceRecoverCacheHits += prunedAttempt.choiceRecoverCacheHits;
    choiceRecoverCacheMisses += prunedAttempt.choiceRecoverCacheMisses;

    const bool replayedOnlyCommittedEdits =
        prunedAttempt.recoveryEdits.size() ==
            prunedSpec.committedRecoveryEdits.size() &&
        std::equal(prunedAttempt.recoveryEdits.begin(),
                   prunedAttempt.recoveryEdits.end(),
                   prunedSpec.committedRecoveryEdits.begin(),
                   same_syntax_script_entry);
    if (prunedAttempt.fullMatch && replayedOnlyCommittedEdits &&
        prunedAttempt.recoveryEdits.size() < attempt.recoveryEdits.size()) {
      return prunedAttempt;
    }
    return std::nullopt;
  };

  bool changed = true;
  while (changed && attempt.recoveryEdits.size() > 1u) {
    changed = false;
    for (std::size_t i = 0; i < attempt.recoveryEdits.size(); ++i) {
      if (auto prunedAttempt = tryPrunedEditSet(i)) {
        attempt = std::move(*prunedAttempt);
        changed = true;
        break;
      }
    }
  }

  if (attempt.recoveryEdits.size() < editCountBeforePruning) {
    PEGIUM_STEP_TRACE_INC(
        StepCounter::MinimizeRecoveryEditsDropped,
        editCountBeforePruning - attempt.recoveryEdits.size());
  }
  return attempt;
}

/// Structural signature of an out-of-window fuzzy *fold*: a destructive prefix
/// Delete run immediately followed by a Replaced whose `beginOffset` equals the
/// preceding Delete's `endOffset` (the fold splices the trailing deleted
/// codepoint(s) into a single fuzzy keyword Replace), with that Replace landing
/// at or beyond the window's `maxCursorOffset` (i.e. out of the active window).
///
/// This is the shape produced when the greedy descent folds the last prefix
/// codepoint into a fuzzy keyword match instead of deleting it outright, hiding
/// a competing pure-delete candidate. It is the trigger for the no-fold sibling
/// probe; it does NOT depend on any specific grammar or literal.
[[nodiscard]] bool
matches_out_of_window_fuzzy_fold_signature(
    std::span<const SyntaxScriptEntry> recoveryEdits,
    TextOffset windowMaxCursorOffset) noexcept {
  for (std::size_t i = 1; i < recoveryEdits.size(); ++i) {
    const auto &prev = recoveryEdits[i - 1];
    const auto &cur = recoveryEdits[i];
    if (prev.kind == ParseDiagnosticKind::Deleted &&
        cur.kind == ParseDiagnosticKind::Replaced &&
        cur.beginOffset == prev.endOffset &&
        cur.beginOffset >= windowMaxCursorOffset) {
      return true;
    }
  }
  return false;
}

/// True when the committed edit script carries the whole-keyword fuzzy-Replace
/// fold signature: a non-empty Replaced edit immediately followed by an
/// Inserted at its end. That is the shape the greedy descent produces when it
/// fuzzy-Replaces a whole keyword (folding the would-be next token into it) and
/// then synthesises the real next element — when a keep-keyword split-Insert may
/// be strictly more faithful. The trigger for the forbid-Replace sibling probe;
/// grammar/literal agnostic.
[[nodiscard]] bool matches_whole_keyword_fuzzy_replace_with_continuation_signature(
    std::span<const SyntaxScriptEntry> recoveryEdits) noexcept {
  bool sawNonEmptyReplace = false;
  for (std::size_t i = 0; i < recoveryEdits.size(); ++i) {
    const auto &cur = recoveryEdits[i];
    if (cur.kind == ParseDiagnosticKind::Replaced &&
        cur.endOffset > cur.beginOffset) {
      sawNonEmptyReplace = true;
    }
    if (i > 0) {
      const auto &prev = recoveryEdits[i - 1];
      // The fold synthesised the real continuation right after the keyword.
      if (prev.kind == ParseDiagnosticKind::Replaced &&
          prev.endOffset > prev.beginOffset &&
          cur.kind == ParseDiagnosticKind::Inserted &&
          cur.beginOffset == prev.endOffset) {
        return true;
      }
    }
    // The fold swallowed a glued continuation (e.g. keyword `module` plus name
    // `b` written `Moduleb`), so the real continuation is later deleted instead.
    // A keep-keyword split-Insert may be strictly more faithful; let the probe's
    // keep-iff-better guard decide.
    if (sawNonEmptyReplace && cur.kind == ParseDiagnosticKind::Deleted) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] std::optional<RecoveryAttempt> try_recovery_window(
    const grammar::ParserRule &entryRule, const Skipper &skipper,
    const ParseOptions &options, const text::TextSnapshot &text,
    const RecoveryAttemptSpec &spec, const RecoveryAttempt &selectedAttempt,
    bool continuingRecovery,
    std::uint32_t &recoveryAttemptRuns,
    std::uint64_t &choiceRecoverCacheHits,
    std::uint64_t &choiceRecoverCacheMisses,
    const utils::CancellationToken &cancelToken,
    RecoveryParseCachePool *cachePool) {
  ++recoveryAttemptRuns;
  PEGIUM_STEP_TRACE_INC(StepCounter::RecoveryPhaseRuns);
  auto primaryAttempt = execute_recovery_parse(entryRule, skipper, options, text,
                                             spec, cancelToken, cachePool);
  classify_recovery_attempt(primaryAttempt);
  trace_recovery_attempt(primaryAttempt, spec);
  choiceRecoverCacheHits += primaryAttempt.choiceRecoverCacheHits;
  choiceRecoverCacheMisses += primaryAttempt.choiceRecoverCacheMisses;

  // Minimize the edit set before the ranker sees the attempt.
  primaryAttempt = minimize_recovery_edits(
      entryRule, skipper, options, text, spec, std::move(primaryAttempt),
      choiceRecoverCacheHits, choiceRecoverCacheMisses, cancelToken, cachePool);

  // Post-greedy strategy probes re-parse the same window with one policy axis
  // perturbed, looking for a fullMatch the greedy primary missed. They are
  // intentionally NOT counted against the attempt budget (it was already paid to
  // obtain the primary). All probes share one body — re-parse, classify, trace,
  // accumulate the choice-cache stats, keep the result only if it reaches
  // fullMatch and outranks the current primary — so it lives in one helper;
  // each probe owns only its trigger, its perturbed option, and its counters.
  // Returns true when it replaced the primary (for a per-probe "win" counter).
  const auto runProbeAndKeepIfBetter =
      [&](const ParseOptions &probeOptions,
          const RecoveryAttemptSpec &probeSpec) {
    PEGIUM_STEP_TRACE_INC(StepCounter::RecoveryPhaseRuns);
    auto attempt = execute_recovery_parse(entryRule, skipper, probeOptions, text,
                                          probeSpec, cancelToken, cachePool);
    classify_recovery_attempt(attempt);
    trace_recovery_attempt(attempt, spec);
    choiceRecoverCacheHits += attempt.choiceRecoverCacheHits;
    choiceRecoverCacheMisses += attempt.choiceRecoverCacheMisses;
    if (attempt.fullMatch &&
        is_better_recovery_attempt(attempt, primaryAttempt)) {
      primaryAttempt = std::move(attempt);
      return true;
    }
    return false;
  };

  // Minimal-edit probe. A greedy path can over-spend edits, or settle for a
  // non-credible multi-edit cascade that hides a single-edit fullMatch (e.g. a
  // 1-cost fuzzy `extend -> extends` Replace hidden behind a 3-insert primary).
  // Re-running with the edit budget tightened to "committed prefix + 1 new edit"
  // exposes the minimal candidate to the shared ranker.
  const bool minimalProbeWorthRunning =
      (primaryAttempt.fullMatch ||
       primaryAttempt.status ==
           RecoveryAttemptStatus::RecoveredButNotCredible) &&
      primaryAttempt.recoveryEdits.size() > 1u &&
      options.maxRecoveryEditsPerAttempt > 1u;
  if (minimalProbeWorthRunning) {
    PEGIUM_STEP_TRACE_INC(StepCounter::MinimalEditProbeRuns);
    ParseOptions minimalOptions = options;
    minimalOptions.maxRecoveryEditsPerAttempt =
        static_cast<std::uint32_t>(spec.committedRecoveryEdits.size()) + 1u;
    if (runProbeAndKeepIfBetter(minimalOptions, spec)) {
      PEGIUM_STEP_TRACE_INC(StepCounter::MinimalEditProbeWins);
    }
  }

  // Forbid-consecutive-deletes probe. On a non-fullMatch primary classified
  // RecoveredButNotCredible/StrictFailure whose edits include a delete, an
  // Insert-only repair may reach fullMatch where the greedy delete-scan produced
  // a non-credible "skip ahead" (e.g. a missing `}` whose Insert lands cleanly).
  // Evaluated AFTER the minimal probe and re-reading the live primaryAttempt, so
  // a fullMatch produced above disables this probe.
  const bool alternativeProbeWorthRunning =
      !primaryAttempt.fullMatch &&
      (primaryAttempt.status ==
           RecoveryAttemptStatus::RecoveredButNotCredible ||
       primaryAttempt.status == RecoveryAttemptStatus::StrictFailure);
  if (alternativeProbeWorthRunning) {
    const bool primaryHasDeletes = std::ranges::any_of(
        primaryAttempt.recoveryEdits, [](const auto &edit) noexcept {
          return edit.kind == ParseDiagnosticKind::Deleted;
        });
    if (primaryHasDeletes && options.maxConsecutiveCodepointDeletes > 0u) {
      ParseOptions probe = options;
      probe.maxConsecutiveCodepointDeletes = 0u;
      runProbeAndKeepIfBetter(probe, spec);
    }
  }

  // No-fold sibling probe. A fullMatch primary may have folded the trailing
  // prefix codepoint(s) into an out-of-window fuzzy keyword Replace (candidate
  // B) instead of deleting them outright (candidate A). The two can tie on
  // editCost, in which case the parsimony tie-break (fewer edits) prefers the
  // pure-delete A — but the greedy descent never generates A because it stops
  // at the first credible fullMatch. Re-descend the same window with the fold
  // carve-out forced off so a pure-delete A is generated and handed to the
  // shared ranker. Bounded: at most one extra (uncounted) re-parse per window,
  // only when the primary carries the fold signature.
  if (primaryAttempt.fullMatch &&
      matches_out_of_window_fuzzy_fold_signature(
          primaryAttempt.recoveryEdits, spec.window.maxCursorOffset) &&
      !spec.probeAxes.forbidOutOfWindowFuzzyFold) {
    RecoveryAttemptSpec probeSpec = spec;
    probeSpec.probeAxes.forbidOutOfWindowFuzzyFold = true;
    runProbeAndKeepIfBetter(options, probeSpec);
  }

  // Forbid-Replace sibling probe. A fullMatch may have fuzzy-Replaced a whole
  // keyword (folding the next token into it) then synthesised the real next
  // element, when a keep-keyword split-Insert would be strictly more faithful.
  // Re-descend with the whole-keyword fuzzy Replace forbidden so the split-Insert
  // is generated and handed to the shared ranker; kept only if it reaches
  // fullMatch and outranks — so a keyword-only grammar, where the split-Insert
  // leaves the extra char unconsumed (non-fullMatch), stays on the Replace.
  if (primaryAttempt.fullMatch &&
      matches_whole_keyword_fuzzy_replace_with_continuation_signature(
          primaryAttempt.recoveryEdits) &&
      !spec.probeAxes.forbidWholeKeywordFuzzyReplace) {
    RecoveryAttemptSpec probeSpec = spec;
    probeSpec.probeAxes.forbidWholeKeywordFuzzyReplace = true;
    runProbeAndKeepIfBetter(options, probeSpec);
  }

  return consider_window_attempt_candidate(std::move(primaryAttempt),
                                           selectedAttempt, continuingRecovery);
}

[[nodiscard]] bool preserves_committed_replay_prefix(
    const RecoveryAttempt &candidate,
    const RecoveryAttempt &selectedAttempt,
    CommittedPrefixContract &contract) noexcept;

[[nodiscard]] bool rewrites_committed_replay_boundary(
    const RecoveryAttempt &candidate,
    const CommittedPrefixContract &contract) noexcept;

[[nodiscard]] bool extends_current_recovery_site(
    const RecoveryAttempt &candidate,
    const CommittedPrefixContract &contract) noexcept {
  if (candidate.recoveryEdits.size() <= contract.preservedEditCount) {
    return false;
  }
  return candidate.parsedLength > contract.resumeFloor;
}

/// Single decision predicate for window-attempt acceptance, returning the
/// accepted attempt or `nullopt`. Two admission paths share the same
/// committed-prefix / boundary-rewrite / continuation gates; only the final
/// non-continuing ranking rule differs. A `Selectable` candidate replaces the
/// selected attempt on the central
/// `is_better_recovery_attempt` ranking; a non-selectable but entry-rule-
/// matched fallback may replace a non-fullMatch selected attempt only when it
/// strictly outranks an existing non-credible fallback.
[[nodiscard]] std::optional<RecoveryAttempt> consider_window_attempt_candidate(
    RecoveryAttempt attempt, const RecoveryAttempt &selectedAttempt,
    bool continuingRecovery) {
  const bool selectable = is_selectable_recovery_attempt(attempt);
  const bool fallbackEligible =
      !selectable && attempt.entryRuleMatched &&
      (continuingRecovery ||
       satisfies_non_credible_fallback_contract(attempt, selectedAttempt));
  if (!selectable && !fallbackEligible) {
    PEGIUM_RECOVERY_TRACE("[parser attempt] rejected for selection status=",
                          recovery_attempt_status_name(attempt.status));
    return std::nullopt;
  }

  CommittedPrefixContract committedReplayContract;
  if (!preserves_committed_replay_prefix(attempt, selectedAttempt,
                                         committedReplayContract) ||
      rewrites_committed_replay_boundary(attempt, committedReplayContract)) {
    return std::nullopt;
  }

  bool accept = false;
  if (continuingRecovery) {
    // A candidate that does not extend the current recovery site cannot
    // accept under continuation.
    accept = extends_current_recovery_site(attempt, committedReplayContract);
  } else if (selectable) {
    accept = is_better_recovery_attempt(attempt, selectedAttempt);
  } else {
    // Non-credible fallback, non-continuing: only beat a non-credible
    // selected attempt when strictly better.
    accept = selectedAttempt.status !=
                 RecoveryAttemptStatus::RecoveredButNotCredible ||
             is_better_recovery_attempt(attempt, selectedAttempt);
  }
  if (accept) {
    return std::move(attempt);
  }
  return std::nullopt;
}

[[nodiscard]] constexpr bool same_syntax_script_entry(
    const SyntaxScriptEntry &lhs,
    const SyntaxScriptEntry &rhs) noexcept {
  return lhs.kind == rhs.kind && lhs.offset == rhs.offset &&
         lhs.beginOffset == rhs.beginOffset && lhs.endOffset == rhs.endOffset &&
         elements_equivalent_for_replay(lhs.element, rhs.element) &&
         lhs.message == rhs.message;
}

[[nodiscard]] bool
can_try_prefix_delete_retry(const grammar::ParserRule &entryRule,
                            const RecoveryAttemptSpec &spec) noexcept {
  // The grammar-shape predicate is in `RecoveryAnalysis` so the
  // orchestrator no longer carries 100+ lines of recursive grammar
  // introspection. The recovery-side condition (window starts at 0
  // and admits an edit at offset 0) stays here.
  return spec.window.beginOffset == 0 && spec.window.editFloorOffset == 0 &&
         entry_rule_has_unguarded_leading_visible_entry(entryRule);
}

[[nodiscard]] bool starts_with_root_insert_then_destructive_suffix_edit(
    std::span<const SyntaxScriptEntry> recoveryEdits,
    TextOffset baseOffset) noexcept {
  if (recoveryEdits.empty()) {
    return false;
  }
  if (const auto &first = recoveryEdits.front();
      first.kind != ParseDiagnosticKind::Inserted ||
      first.beginOffset != baseOffset) {
    return false;
  }
  return std::ranges::any_of(recoveryEdits.begin() + 1, recoveryEdits.end(),
                             [baseOffset](const SyntaxScriptEntry &entry) noexcept {
                               return is_destructive_edit_kind(entry.kind) &&
                                      entry.beginOffset > baseOffset;
                             });
}

[[nodiscard]] bool
try_prefix_delete_retry_entry_rule(RecoveryContext &ctx,
                                   const grammar::ParserRule &entryRule) {
  if (!ctx.canDelete() || ctx.cursor() != ctx.begin) {
    return false;
  }

  const bool savedAllowInsert = ctx.editPolicy.allowInsert;
  const bool previousSkipAfterDelete = ctx.skipAfterDelete;
  auto deleteOnlyGuard = ctx.withEditPermissions(false, true);
  (void)deleteOnlyGuard;
  return detail::recover_by_guarded_delete_retry(
      ctx,
      [](const detail::DeleteRetryVisitState &) noexcept { return true; },
      [&](const detail::DeleteRetryVisitState &) {
        const auto retryEditCountBefore = ctx.recoveryEditCount();
        const auto retryStartOffset = ctx.cursorOffset();
        const auto retryVisibleLeafCount = ctx.failureHistorySize();
        detail::ProbeRestoreScope guard{ctx};
        ctx.skip();
        ctx.skipAfterDelete = previousSkipAfterDelete;
        auto retryEditGuard = ctx.withEditPermissions(savedAllowInsert, false);
        (void)retryEditGuard;
        const bool matched = entryRule.recover(ctx);
        if (matched) {
          ctx.skip();
          const auto retryEdits = ctx.recoveryEditsView();
          const bool retryInsertThenDestructiveSuffix =
              starts_with_root_insert_then_destructive_suffix_edit(
                  retryEdits.subspan(retryEditCountBefore), retryStartOffset);
          const auto recoveredVisibleLeafCount = ctx.failureHistorySize();
          const bool exposesStructuredEntry =
              recoveredVisibleLeafCount >= retryVisibleLeafCount + 2u ||
              (ctx.cursor() == ctx.end &&
               recoveredVisibleLeafCount > retryVisibleLeafCount);
          if (ctx.cursor() == ctx.end && exposesStructuredEntry &&
              !retryInsertThenDestructiveSuffix) {
            guard.commit();
            return true;
          }
        }
        ctx.skipAfterDelete = previousSkipAfterDelete;
        return false;
      },
      {.extendThroughHiddenTrivia = false});
}

} // namespace

RecoveryWindow plan_recovery_window(const FailureSnapshot &failureSnapshot,
                                    const RecoveryAttempt &selectedAttempt,
                                    const ParseOptions &options,
                                    std::uint32_t forwardTokenCount) noexcept {
  const auto prefixContract =
      recovery_window_prefix_contract(failureSnapshot, selectedAttempt);
  const auto backwardTokenCount =
      std::max<std::uint32_t>(1u, options.recoveryWindowTokenCount);
  auto window =
      compute_recovery_window(failureSnapshot, backwardTokenCount,
                              forwardTokenCount, prefixContract.editFloorOffset);
  window.stablePrefixOffset = prefixContract.stablePrefixOffset;
  window.hasStablePrefix = prefixContract.hasStablePrefix;
  return window;
}

static void fill_committed_recovery_prefix(RecoveryAttemptSpec &spec,
                                           const RecoveryAttempt &selectedAttempt) {
  const auto contract = committed_replay_prefix_contract(selectedAttempt);
  spec.committedRecoveryResumeFloor = contract.resumeFloor;
  spec.committedRecoveryEdits.clear();
  if (contract.preservedEditCount == 0u) {
    return;
  }
  spec.committedRecoveryEdits.assign(
      selectedAttempt.recoveryEdits.begin(),
      selectedAttempt.recoveryEdits.begin() +
          static_cast<std::ptrdiff_t>(contract.preservedEditCount));
}

RecoveryAttempt
execute_recovery_parse(const grammar::ParserRule &entryRule,
                     const Skipper &skipper, const ParseOptions &options,
                     const text::TextSnapshot &text,
                     const RecoveryAttemptSpec &spec,
                     const utils::CancellationToken &cancelToken,
                     RecoveryParseCachePool *cachePool) {
  RecoveryAttempt attempt;
  attempt.stablePrefixOffset = spec.window.stablePrefixOffset;
  attempt.hasStablePrefix = spec.window.hasStablePrefix;
  attempt.configuredMaxEditCost = options.maxRecoveryEditCost;
  const auto inputSize = static_cast<TextOffset>(text.size());

  FailureHistoryRecorder failureRecorder(text.view().data());
  auto cst = std::make_unique<RootCstNode>(text);
  CstBuilder builder(*cst);
  RecoveryContext parseCtx{builder, skipper, failureRecorder, cancelToken};
  parseCtx.setCommittedRecoveryPrefix(spec.committedRecoveryEdits,
                                      spec.committedRecoveryResumeFloor);
  parseCtx.setEditWindow(RecoveryContext::EditWindow{
      .beginOffset = spec.window.beginOffset,
      .editFloorOffset = spec.window.editFloorOffset,
      .maxCursorOffset = spec.window.maxCursorOffset,
      .forwardTokenCount = spec.window.forwardTokenCount,
      .replayForwardTokenCount = std::min(
          spec.window.forwardTokenCount, options.recoveryStabilityTokenCount)});
  parseCtx.editPolicy.trackEditState = true;
  parseCtx.recoveryState.windowReplay.inRecoveryPhase = false;
  parseCtx.editPolicy.maxConsecutiveCodepointDeletes =
      options.maxConsecutiveCodepointDeletes;
  parseCtx.forbidOutOfWindowFuzzyFold = spec.probeAxes.forbidOutOfWindowFuzzyFold;
  parseCtx.forbidWholeKeywordFuzzyReplace =
      spec.probeAxes.forbidWholeKeywordFuzzyReplace;
  parseCtx.stabilityTokenCount = options.recoveryStabilityTokenCount;
  parseCtx.editPolicy.maxEditsPerAttempt = options.maxRecoveryEditsPerAttempt;
  parseCtx.editPolicy.maxEditCost = options.maxRecoveryEditCost;
  parseCtx.maxResyncSkipCodepoints = options.maxResyncSkipCodepoints;
  parseCtx.maxRecoveryRuleEntries = options.maxRecoveryRuleEntries;
  if (cachePool != nullptr) {
    // Reuse the driver-owned cache storage across this window's re-parses. The
    // choice cache is cleared so this probe starts from an empty cache (behaviour
    // identical to a per-attempt cache, only the allocation is reused); the fuzzy
    // cache is shared warm (its entries are a pure function of their key).
    cachePool->choice.reset();
    parseCtx.usePooledRecoveryCaches(&cachePool->choice, &cachePool->fuzzy);
  }
  parseCtx.choiceRecoverCache().setDisabled(
      options.diagnostics.recoveryCacheDisabled);

  parseCtx.skip();
  const auto attemptCheckpoint = parseCtx.mark();
  // `recoveryRuleEntries` is a monotonic per-attempt budget that
  // `RecoveryContext::rewind` intentionally does NOT restore. Snapshot it at the
  // checkpoint so the weak-match fallback below can re-descend with the same
  // budget the original descent had (see the prefix-delete-retry block).
  const auto entryRuleEntriesAtCheckpoint = parseCtx.recoveryRuleEntries;
  attempt.entryRuleMatched = entryRule.recover(parseCtx);
  const auto failureParsedLength = parseCtx.cursorOffset();
  const auto failureMaxCursorOffset = parseCtx.maxCursorOffset();
  auto failureRecoveryEdits = parseCtx.snapshotRecoveryEdits();
  const auto failureEditCost = parseCtx.currentEditCost();
  const auto failureEditCount = parseCtx.currentEditCount();
  const auto failureCompletedRecoveryWindows =
      parseCtx.completedRecoveryWindowCount();
  const auto failureReachedRecoveryTarget = parseCtx.hasReachedRecoveryTarget();
  const auto failureStableAfterRecovery = parseCtx.isStableAfterRecovery();
  const bool hasRootOffsetEdit =
      !failureRecoveryEdits.empty() &&
      failureRecoveryEdits.front().beginOffset == 0;
  const bool weakMatchedZeroPrefixAttempt =
      attempt.entryRuleMatched && hasRootOffsetEdit &&
      failureParsedLength == 0 &&
      !failureReachedRecoveryTarget && !failureStableAfterRecovery;
  const bool rootInsertThenDestructiveSuffixAttempt =
      attempt.entryRuleMatched &&
      starts_with_root_insert_then_destructive_suffix_edit(
          failureRecoveryEdits, /*baseOffset=*/0);
  if (!attempt.entryRuleMatched || weakMatchedZeroPrefixAttempt ||
      rootInsertThenDestructiveSuffixAttempt) {
    const bool hadDirectMatch = attempt.entryRuleMatched;
    parseCtx.rewind(attemptCheckpoint);
    bool retrySucceeded = false;
    if (can_try_prefix_delete_retry(entryRule, spec)) {
      PEGIUM_STEP_TRACE_INC(StepCounter::RootPrefixRetryRuns);
      retrySucceeded = try_prefix_delete_retry_entry_rule(parseCtx, entryRule);
      attempt.entryRuleMatched = retrySucceeded;
      if (retrySucceeded) {
        PEGIUM_STEP_TRACE_INC(StepCounter::RootPrefixRetryWins);
      }
    }
    // Key the rebuild on whether the retry actually produced a match, not on
    // `entryRuleMatched`: when the retry is not applicable that flag still holds
    // the stale pre-rewind `true`, which would skip the rebuild and emit a
    // degenerate strict-failure attempt for a repair we actually found.
    if (!retrySucceeded && hadDirectMatch) {
      // Rebuild the direct match the rewind above discarded. The failed
      // prefix-delete retry already restored the cursor and edit script (its own
      // rewind), but NOT the monotonic `recoveryRuleEntries` budget, which
      // `RecoveryContext::rewind` deliberately preserves. Re-descending with the
      // inflated counter can trip `maxRecoveryRuleEntries` earlier than the
      // original descent did and yield a degraded/empty CST whose shape depends
      // on how much budget the failed retry happened to consume. Re-establish
      // the exact pre-descent state — cursor/edits AND the original budget — so
      // the rebuild reproduces the direct match deterministically.
      parseCtx.rewind(attemptCheckpoint);
      parseCtx.recoveryRuleEntries = entryRuleEntriesAtCheckpoint;
      attempt.entryRuleMatched = entryRule.recover(parseCtx);
    }
  }

  parseCtx.skip();
  parseCtx.finalizeRecoveryAtEof();
  attempt.parsedLength =
      attempt.entryRuleMatched ? parseCtx.cursorOffset() : failureParsedLength;
  attempt.lastVisibleCursorOffset = parseCtx.lastVisibleCursorOffset();
  attempt.recoveryEdits = normalize_syntax_script(
      attempt.entryRuleMatched ? parseCtx.takeRecoveryEdits()
                               : failureRecoveryEdits);
  attempt.fullMatch =
      attempt.entryRuleMatched && attempt.parsedLength == inputSize;
  attempt.maxCursorOffset = attempt.entryRuleMatched
                                ? parseCtx.maxCursorOffset()
                                : failureMaxCursorOffset;
  if (attempt.entryRuleMatched) {
    auto window = spec.window;
    if (const auto replayForwardCount = parseCtx.replayForwardTokenCount();
        replayForwardCount.has_value()) {
      window.forwardTokenCount = *replayForwardCount;
    }
    attempt.replayWindow = window;
  }
  if (!attempt.fullMatch && parseCtx.isFailureHistoryRecordingEnabled()) {
    auto failureSnapshotOffset =
        attempt.entryRuleMatched
            ? std::max(attempt.parsedLength, attempt.maxCursorOffset)
            : attempt.maxCursorOffset;
    auto trackedSnapshot = failureRecorder.snapshot(failureSnapshotOffset);
    if (attempt.entryRuleMatched &&
        attempt.parsedLength < failureSnapshotOffset &&
        should_fallback_to_parsed_length_snapshot(trackedSnapshot,
                                                  attempt.parsedLength)) {
      failureSnapshotOffset = attempt.parsedLength;
      trackedSnapshot = failureRecorder.snapshot(failureSnapshotOffset);
    }
    // Lazy committed-CST snapshot. `snapshot_from_committed_cst` walks every
    // committed leaf and allocates a fresh leaf vector on each non-full attempt
    // (including the uncounted minimize/probe re-parses), yet its result can
    // only be selected over the tracked snapshot when the tracked one is
    // deficient at the failure horizon. A committed snapshot's last visible leaf
    // cannot end past `failureSnapshotOffset` (the committed CST spans at most
    // that far), so once the tracked snapshot already reaches the horizon with a
    // failure token it dominates on every selection axis below and the committed
    // walk can be skipped entirely — identical outcome, no walk, no allocation.
    const bool trackedDominatesFailureHorizon =
        trackedSnapshot.hasFailureToken &&
        last_failure_leaf_end_offset(trackedSnapshot) >= failureSnapshotOffset;
    if (!trackedDominatesFailureHorizon) {
      auto committedSnapshot =
          snapshot_from_committed_cst(*cst, failureSnapshotOffset);
      // Committed wins iff its last visible failure leaf ends strictly later,
      // or ties while it is the only one carrying usable failure history
      // (tracked empty, or tracked lacks a failure token that committed has).
      const auto committedEnd = last_failure_leaf_end_offset(committedSnapshot);
      const auto trackedEnd = last_failure_leaf_end_offset(trackedSnapshot);
      const bool committedWins =
          committedEnd > trackedEnd ||
          (committedEnd == trackedEnd &&
           !committedSnapshot.failureLeafHistory.empty() &&
           (trackedSnapshot.failureLeafHistory.empty() ||
            (!trackedSnapshot.hasFailureToken &&
             committedSnapshot.hasFailureToken)));
      if (committedWins) {
        trackedSnapshot = std::move(committedSnapshot);
      }
    }
    attempt.failureSnapshot = std::move(trackedSnapshot);
  }
  attempt.editCost =
      attempt.entryRuleMatched ? parseCtx.currentEditCost() : failureEditCost;
  attempt.editCount =
      attempt.entryRuleMatched ? parseCtx.currentEditCount() : failureEditCount;
  attempt.completedRecoveryWindows =
      attempt.entryRuleMatched ? parseCtx.completedRecoveryWindowCount()
                               : failureCompletedRecoveryWindows;
  attempt.reachedRecoveryTarget = attempt.entryRuleMatched
                                      ? parseCtx.hasReachedRecoveryTarget()
                                      : failureReachedRecoveryTarget;
  attempt.stableAfterRecovery = attempt.entryRuleMatched
                                    ? parseCtx.isStableAfterRecovery()
                                    : failureStableAfterRecovery;
  (void)builder.getRootCstNode();
  attempt.cst = std::move(cst);
  attempt.choiceRecoverCacheHits = parseCtx.choiceRecoverCache().hits();
  attempt.choiceRecoverCacheMisses = parseCtx.choiceRecoverCache().misses();
  return attempt;
}

RecoverySearchRunResult
orchestrate_recovery_search(const grammar::ParserRule &entryRule,
                    const Skipper &skipper, const ParseOptions &options,
                    const text::TextSnapshot &text,
                    const utils::CancellationToken &cancelToken) {
  RecoverySearchRunResult result;

  PEGIUM_STEP_TRACE_INC(StepCounter::ParsePhaseRuns);
  auto strictFailureStage =
      parse_strict_capturing_failure(entryRule, skipper, options, text, cancelToken);
  result.selectedAttempt = std::move(strictFailureStage.strictAttempt);
  result.failureVisibleCursorOffset =
      strictFailureStage.failureVisibleCursorOffset;
  result.strictParseRuns = 1u;
  if (!strictFailureStage.failureSnapshot.has_value()) {
    return result;
  }

  if (result.selectedAttempt.fullMatch) {
    return result;
  }

  FailureSnapshot currentFailure = *std::move(strictFailureStage.failureSnapshot);
  TextOffset previousRecoveryEditFloor = 0;
  // One set of recovery caches reused across every window probe of this search,
  // so each re-parse reuses these allocations instead of building cold caches.
  RecoveryParseCachePool cachePool;
  // Search constants: pure functions of the (const) options, identical every
  // iteration — computed once outside the loop.
  const auto windowTokenCount =
      std::max<std::uint32_t>(1u, options.recoveryWindowTokenCount);
  const auto widenedTokenCount =
      std::max(options.maxRecoveryWindowTokenCount, windowTokenCount);
  while (true) {
    RecoveryWindow committedWindow{};
    bool siteCommitted = false;
    // The committed-prefix replay info depends on the currently selected
    // attempt, which only changes when a window commits (after which we
    // break the inner loop). Compute it once per outer iteration instead of
    // re-copying the prefix vector for every widening attempt.
    RecoveryAttemptSpec spec{};
    const bool continuingRecovery = !result.selectedWindows.empty();
    fill_committed_recovery_prefix(spec, result.selectedAttempt);
    std::uint32_t forwardTokenCount = windowTokenCount;
    while (true) {
      if (recovery_attempt_budget_exhausted(result, options)) {
        return result;
      }
      const auto window = plan_recovery_window(
          currentFailure, result.selectedAttempt, options, forwardTokenCount);
      // editFloorOffset is forwardTokenCount-invariant (compute_recovery_window
      // threads forwardTokenCount only into window.forwardTokenCount), so the
      // first planned window of this site carries the same edit floor a separate
      // pre-pass would. Stall out here — before counting this window — when a
      // previously committed site failed to advance the edit floor.
      if (forwardTokenCount == windowTokenCount &&
          result.recoveryWindowsTried > 0u &&
          window.editFloorOffset <= previousRecoveryEditFloor) {
        return result;
      }
      trace_recovery_window(result.recoveryWindowsTried, forwardTokenCount,
                            window, result.selectedWindows.size());
      ++result.recoveryWindowsTried;

      spec.window = window;
      auto acceptedAttempt = try_recovery_window(
          entryRule, skipper, options, text, spec, result.selectedAttempt,
          continuingRecovery, result.recoveryAttemptRuns,
          result.choiceRecoverCacheHits, result.choiceRecoverCacheMisses,
          cancelToken, &cachePool);
      if (acceptedAttempt.has_value()) {
        result.selectedAttempt = std::move(*acceptedAttempt);
#if defined(PEGIUM_ENABLE_RECOVERY_TRACE)
        PEGIUM_RECOVERY_TRACE(
            "[parser window] accepted begin=", window.beginOffset,
            " max=", window.maxCursorOffset,
            " full=", result.selectedAttempt.fullMatch,
            " len=", result.selectedAttempt.parsedLength,
            " cost=", result.selectedAttempt.editCost,
            " status=",
            recovery_attempt_status_name(result.selectedAttempt.status));
        trace_recovery_json("[parser selected-attempt]",
                            recovery_attempt_to_json(result.selectedAttempt));
#endif
        committedWindow = result.selectedAttempt.replayWindow.value_or(window);
        result.selectedWindows.push_back(committedWindow);
        siteCommitted = true;
        break;
      }
      if (forwardTokenCount >= widenedTokenCount) {
        break;
      }
      forwardTokenCount = widenedTokenCount;
    }

    if (!siteCommitted) {
      return result;
    }
    if (result.selectedAttempt.fullMatch) {
      return result;
    }
    if (!result.selectedAttempt.failureSnapshot.has_value()) {
      return result;
    }
    previousRecoveryEditFloor = committedWindow.editFloorOffset;
    currentFailure = *result.selectedAttempt.failureSnapshot;
  }
}


void classify_recovery_attempt(RecoveryAttempt &attempt) noexcept {
  using enum RecoveryAttemptStatus;
  // Refresh the cached facts on every classification call. Tests
  // frequently mutate attempt fields between calls (or build attempts
  // by hand and classify them); always recomputing keeps the cache
  // consistent without leaking complexity to callers.
  attempt.facts = derive_attempt_facts(attempt);
  const auto &facts = attempt.facts;

  if (!attempt.entryRuleMatched) {
    attempt.status = StrictFailure;
    return;
  }

  // A full match is the strongest possible success signal; the budget exists
  // to cap exploratory edits, not to reject a complete parse.
  const bool singleInsert =
      attempt.recoveryEdits.size() == 1u &&
      attempt.recoveryEdits.front().kind == ParseDiagnosticKind::Inserted;
  const bool allowsLocalGapRecoveryWithoutStablePrefix =
      !attempt.hasStablePrefix && !attempt.stableAfterRecovery &&
      attempt.reachedRecoveryTarget && singleInsert;
  if (attempt.editCost > attempt.configuredMaxEditCost &&
      !attempt.fullMatch &&
      !facts.preservesStablePrefixBeforeFirstEdit &&
      !allowsLocalGapRecoveryWithoutStablePrefix) {
    attempt.status =
        facts.hasEdits ? RecoveredButNotCredible : StrictFailure;
    return;
  }

  const bool deleteOnlyWithoutContinuation =
      facts.hasDeleteOnlyRecovery && !facts.continuesAfterFirstEdit;
  if (attempt.fullMatch || attempt.stableAfterRecovery) {
    if (!attempt.fullMatch && facts.hasEditPastReplayWindowHorizon) {
      attempt.status = RecoveredButNotCredible;
      return;
    }
    if (deleteOnlyWithoutContinuation) {
      attempt.status = RecoveredButNotCredible;
      return;
    }
    attempt.status = Selectable;
  } else if (attempt.reachedRecoveryTarget &&
             facts.continuesAfterFirstEdit) {
    attempt.status = Selectable;
  } else if (facts.hasEdits) {
    attempt.status = RecoveredButNotCredible;
  } else {
    attempt.status = StrictFailure;
  }
}

bool is_selectable_recovery_attempt(const RecoveryAttempt &attempt) noexcept {
  return attempt.status == RecoveryAttemptStatus::Selectable;
}

[[nodiscard]] bool admits_non_credible_fallback(
    const RecoveryAttempt &attempt) noexcept {
  if (!attempt.entryRuleMatched ||
      attempt.status != RecoveryAttemptStatus::RecoveredButNotCredible ||
      attempt.recoveryEdits.empty()) {
    return false;
  }

  const auto &facts = attempt.facts;

  if (facts.preservesStablePrefixBeforeFirstEdit) {
    return true;
  }

  // When the entry rule's leading literal failed strictly and was
  // repaired by a fuzzy Replace at offset 0, there is no "stable
  // prefix" by definition — the strictly-matched prefix length is
  // zero. The classic credibility contract rejects such attempts
  // even when the parse subsequently makes real progress (committed
  // multiple statements after the replace, then hit a downstream
  // failure that needs its own recovery window).
  //
  // Admit the attempt as a non-credible fallback when:
  //   - first edit is a Replace at offset 0 (fuzzy keyword repair on
  //     the entry-rule prefix);
  //   - the parser advanced strictly past that Replace (parsedLength
  //     past the replace span). This is a weaker check than the
  //     usual `continuesAfterFirstEdit` because the LAST edit may
  //     be a synthetic insert at the trailing failure cursor (whose
  //     endOffset coincides with parsedLength); requiring progress
  //     past the FIRST edit alone is the right contract here.
  //
  // The next recovery window resumes from the end of the prefix
  // replace (resumeFloor) and the existing edits are preserved
  // (preservedEditCount = full edit set), letting downstream failures
  // open their own credible windows past the entry-rule prefix.
  const bool firstEditIsReplaceAtRoot =
      attempt.recoveryEdits.front().beginOffset == 0 &&
      attempt.recoveryEdits.front().kind == ParseDiagnosticKind::Replaced;
  if (!attempt.hasStablePrefix && firstEditIsReplaceAtRoot &&
      attempt.parsedLength > attempt.recoveryEdits.front().endOffset) {
    return true;
  }

  if (attempt.hasStablePrefix || !facts.hasDeleteOnlyRecovery) {
    return false;
  }

  return true;
}

namespace {

[[nodiscard]] bool preserves_committed_replay_prefix(
    const RecoveryAttempt &candidate,
    const RecoveryAttempt &selectedAttempt,
    CommittedPrefixContract &contract) noexcept {
  contract = committed_replay_prefix_contract(selectedAttempt);
  if (candidate.recoveryEdits.size() < contract.preservedEditCount) {
    return false;
  }
  for (std::size_t index = 0; index < contract.preservedEditCount; ++index) {
    const auto &selectedEntry = selectedAttempt.recoveryEdits[index];
    const auto &candidateEntry = candidate.recoveryEdits[index];
    if (!same_syntax_script_entry(selectedEntry, candidateEntry)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool rewrites_committed_replay_boundary(
    const RecoveryAttempt &candidate,
    const CommittedPrefixContract &contract) noexcept {
  return contract.preservedEditCount < candidate.recoveryEdits.size() &&
         candidate.recoveryEdits[contract.preservedEditCount].beginOffset <
             contract.resumeFloor;
}

// The boundary edit (the one whose endOffset == lastEditOffset) is preserved
// only when the parse made visible progress past it (parsedLength >
// lastEditOffset → resumeFloor = parsedLength, boundary edit endOffset <
// resumeFloor). When the parse stopped at the boundary edit (parsedLength <=
// lastEditOffset → resumeFloor = lastEditOffset), the boundary edit is NOT
// preserved: the next recovery attempt will redo it from scratch.
[[nodiscard]] CommittedPrefixContract
committed_replay_prefix_contract(
    const RecoveryAttempt &selectedAttempt) noexcept {
  CommittedPrefixContract contract;
  if (selectedAttempt.recoveryEdits.empty()) {
    return contract;
  }
  // Reading selectedAttempt.facts is safe here: the only attempt that reaches a
  // contract un-classified is the zero-edit strict attempt, excluded by the
  // empty-edits early return above. Any attempt with edits has been classified
  // (facts populated). See the facts contract in RecoverySearch.hpp.
  contract.resumeFloor =
      std::max(selectedAttempt.parsedLength, selectedAttempt.facts.lastEditOffset);
  while (contract.preservedEditCount < selectedAttempt.recoveryEdits.size() &&
         selectedAttempt.recoveryEdits[contract.preservedEditCount].endOffset <
             contract.resumeFloor) {
    ++contract.preservedEditCount;
  }
  return contract;
}

} // namespace

bool satisfies_non_credible_fallback_contract(
    const RecoveryAttempt &candidate,
    const RecoveryAttempt &selectedAttempt) noexcept {
  CommittedPrefixContract contract;
  if (!admits_non_credible_fallback(candidate) ||
      !preserves_committed_replay_prefix(candidate, selectedAttempt,
                                         contract) ||
      rewrites_committed_replay_boundary(candidate, contract)) {
    return false;
  }

  const auto &candidateFacts = candidate.facts;
  const auto &selectedFacts = selectedAttempt.facts;

  if (selectedFacts.hasEdits &&
      selectedAttempt.status == RecoveryAttemptStatus::RecoveredButNotCredible &&
      contract.preservedEditCount == selectedAttempt.recoveryEdits.size()) {
    const auto committedSourceFloor =
        std::max(selectedAttempt.parsedLength, selectedFacts.lastEditOffset);
    const auto newEdits = std::span(candidate.recoveryEdits)
                              .subspan(contract.preservedEditCount);
    const bool rewritesCommittedSource = std::ranges::any_of(
        newEdits, [&](const SyntaxScriptEntry &entry) noexcept {
          return entry.beginOffset >= committedSourceFloor &&
                 entry.kind != ParseDiagnosticKind::Inserted;
        });
    if (rewritesCommittedSource) {
      return false;
    }
  }

  if (!candidateFacts.hasCursorProgressPastLastEdit ||
      candidate.maxCursorOffset <= contract.resumeFloor) {
    return false;
  }

  if (contract.preservedEditCount == 0u) {
    return !candidateFacts.hasDeleteOnlyRecovery ||
           candidateFacts.continuesAfterFirstEdit;
  }

  return candidate.parsedLength > selectedAttempt.parsedLength;
}

[[nodiscard]] RecoveryKey
recovery_attempt_key(const RecoveryAttempt &attempt) noexcept {
  const bool selectableAttempt =
      attempt.status == RecoveryAttemptStatus::Selectable;
  // Read the two facts directly from the attempt rather than from
  // `attempt.facts`: the cached `facts` is populated by
  // `classify_recovery_attempt`, and a caller that ranks attempts before
  // classification would otherwise see a default zero-edit shape and
  // build a silently wrong key.
  const bool hasEdits = !attempt.recoveryEdits.empty();
  const TextOffset effectiveFirstEditOffset = effective_first_edit_offset(
      hasEdits,
      hasEdits ? attempt.recoveryEdits.front().beginOffset : TextOffset{0},
      attempt.parsedLength);
  return {
      .fullMatch = attempt.fullMatch,
      .matched = selectableAttempt,
      .firstEditOffset = effectiveFirstEditOffset,
      .editCost = attempt.editCost,
      .editCount = attempt.editCount,
      .progressAfterEdits = attempt.parsedLength,
  };
}

bool is_better_recovery_attempt(const RecoveryAttempt &lhs,
                                const RecoveryAttempt &rhs) noexcept {
  return is_better_recovery_key(recovery_attempt_key(lhs),
                                recovery_attempt_key(rhs));
}

} // namespace pegium::parser::detail
