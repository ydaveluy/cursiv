#pragma once

/// Helpers for evaluating and ranking recovery candidates in editable parse
/// modes. The terminal-level *apply* helpers (`can_apply_recovery_match`,
/// `apply_insert_synthetic_gap_and_match_recovery_edit`) live with the
/// terminal combinators in TerminalRecoverySupport.hpp.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

#include <pegium/core/parser/ParseAttempt.hpp>
#include <pegium/core/parser/RecoveryContext.hpp>
#include <pegium/core/parser/ParseMode.hpp>
#include <pegium/core/parser/RecoveryCandidate.hpp>
#include <pegium/core/utils/FunctionRef.hpp>

namespace pegium::parser::detail {

/// True iff `kind` commits the candidate to a strictly different replay
/// prefix. Both `Deleted` and `Replaced` consume source text and shift
/// what a downstream replay must reproduce; an `Inserted` edit alone
/// does not. Recovery's family-redundancy and replay-prefix
/// classifications collapse to this single predicate.
[[nodiscard]] constexpr bool
is_destructive_edit_kind(ParseDiagnosticKind kind) noexcept {
  return kind == ParseDiagnosticKind::Deleted ||
         kind == ParseDiagnosticKind::Replaced;
}

/// Summary of the edit-script slice [`baseEditCount`, end()): the first
/// edit's begin offset, the maximum end offset, and whether any edit in
/// the slice is destructive (`Deleted` or `Replaced`). Both the OrderedChoice/
/// Group/InfixRule and the Repetition `EditableRecoveryCandidate` producers
/// collapse the same loop into this projection.
struct EditSliceSummary {
  TextOffset firstEditBeginOffset = 0;
  TextOffset maxEndOffset = 0;
  bool hasDestructiveEdit = false;
  /// True iff the slice is non-empty and every edit in it is `Deleted`
  /// (no inserts, no replaces). Default `false` on an empty slice so
  /// callers that only consult this when the slice is non-empty get a
  /// safe default.
  bool allDeleted = false;
};

[[nodiscard]] inline EditSliceSummary
summarize_edits_since(std::span<const SyntaxScriptEntry> edits,
                      std::size_t baseEditCount) noexcept {
  EditSliceSummary summary{};
  if (baseEditCount >= edits.size()) {
    return summary;
  }
  summary.firstEditBeginOffset = edits[baseEditCount].beginOffset;
  summary.allDeleted = true;
  for (std::size_t i = baseEditCount; i < edits.size(); ++i) {
    summary.hasDestructiveEdit =
        summary.hasDestructiveEdit || is_destructive_edit_kind(edits[i].kind);
    summary.maxEndOffset = std::max(summary.maxEndOffset, edits[i].endOffset);
    if (edits[i].kind != ParseDiagnosticKind::Deleted) {
      summary.allDeleted = false;
    }
  }
  return summary;
}

[[nodiscard]] inline TextOffset
post_skip_cursor_offset(RecoveryContext &ctx) {
  // RecoveryContext always exposes skip_without_builder, so a side-effect-free
  // trivia skip suffices (no mark/skip/rewind dance needed).
  const auto skipped = ctx.skip_without_builder(ctx.cursor());
  return static_cast<TextOffset>(skipped - ctx.begin);
}

/// Which cursor the candidate's ranking key reads. The per-producer
/// difference is deliberate (see `EditableRecoveryCandidate::keyProgressOffset`);
/// making it an explicit axis of the single construction site keeps a
/// producer from silently inheriting the wrong convention.
enum class KeyProgressConvention : std::uint8_t {
  /// OrderedChoice / Group / InfixRule rank on the post-skip cursor.
  PostSkipCursor,
  /// Repetition ranks on the RAW iteration cursor (Invariant 2:
  /// same-start continuing repairs rank on raw progress, not post-skip).
  RawIterationCursor,
};

/// The single population site for a matched `EditableRecoveryCandidate`:
/// progress offsets, the edit-slice summary of [`baseRecoveryEditCount`,
/// end()), and the replay-prefix classification. Called at the matched
/// cursor, BEFORE any rewind rolls the speculative edits back.
[[nodiscard]] inline EditableRecoveryCandidate
capture_editable_candidate(RecoveryContext &ctx, std::uint32_t baseEditCost,
                           std::size_t baseRecoveryEditCount,
                           KeyProgressConvention convention) {
  EditableRecoveryCandidate candidate;
  candidate.matched = true;
  candidate.postSkipCursorOffset = post_skip_cursor_offset(ctx);
  candidate.keyProgressOffset =
      convention == KeyProgressConvention::RawIterationCursor
          ? ctx.cursorOffset()
          : candidate.postSkipCursorOffset;
  candidate.editCost = ctx.currentEditCost() - baseEditCost;
  candidate.editCount = static_cast<std::uint32_t>(ctx.recoveryEditCount() -
                                                   baseRecoveryEditCount);
  // Both `Deleted` and `Replaced` commit to a strictly different
  // replay prefix than an insert-only candidate, so both belong to
  // the destructive equivalence class consumed by the family-
  // redundancy filters. Treating Replace as non-destructive here
  // misclassifies fuzzy keyword recoveries (e.g. `entit -> entity`)
  // as `NewLocalPrefix`, which silently drops the
  // `extension_outranks_anchor_base` ratio guard and lets a sibling
  // insert-only path with the same cost win on enumeration order.
  bool hasDestructiveEdit = false;
  if (ctx.recoveryEditCount() > baseRecoveryEditCount) {
    const auto editSummary = summarize_edits_since(ctx.recoveryEditsView(),
                                                   baseRecoveryEditCount);
    candidate.firstEditOffset = editSummary.firstEditBeginOffset;
    hasDestructiveEdit = editSummary.hasDestructiveEdit;
    candidate.lastEditEndOffset = editSummary.maxEndOffset;
  }
  candidate.replayPrefix =
      classify_replay_prefix(candidate.editCount != 0u, hasDestructiveEdit);
  return candidate;
}

// The candidate-evaluation body, with the attempt closure type-erased to
// pegium::utils::function_ref<bool()>. Templated on Checkpoint only, so one body
// is emitted per checkpoint type (one, in practice) instead of one per distinct
// attempt closure — collapsing the ~215 near-identical instantiations into one.
// Call sites pass their temporary attempt closure directly; function_ref binds
// it for the duration of the call.
template <typename Checkpoint>
[[nodiscard]] inline EditableRecoveryCandidate
evaluate_editable_recovery_candidate(
    RecoveryContext &ctx, const Checkpoint &entryCheckpoint,
    std::uint32_t baseEditCost, std::size_t baseRecoveryEditCount,
    pegium::utils::function_ref<bool()> runner) {
  EditableRecoveryCandidate candidate;
  const char *const savedFurthestExploredCursor =
      ctx.maxCursor();
  if (runner()) {
    candidate =
        capture_editable_candidate(ctx, baseEditCost, baseRecoveryEditCount,
                                   KeyProgressConvention::PostSkipCursor);
  }
  ctx.rewind(entryCheckpoint);
  ctx.restoreMaxCursor(savedFurthestExploredCursor);
  return candidate;
}

[[nodiscard]] inline RecoveryProbeProgress
capture_recovery_probe_progress(const RecoveryContext &ctx,
                                std::size_t visibleLeafCountBefore) {
  const auto furthestVisibleLeafCount = ctx.furthestFailureHistorySize();
  return {
      .committedOffset = ctx.cursorOffset(),
      .furthestExploredOffset = ctx.maxCursorOffset(),
      .exploredVisibleLeafCount =
          furthestVisibleLeafCount > visibleLeafCountBefore
              ? furthestVisibleLeafCount - visibleLeafCountBefore
              : 0u,
  };
}

// `attempt_parse_without_side_effects` stays with the ranking half (rather than
// with the terminal-level apply helpers in TerminalRecoverySupport.hpp) because
// its sole caller is the structural Group combinator, which already includes
// this header for candidate ranking.
template <Expression E>
[[nodiscard]] inline bool
attempt_parse_without_side_effects(RecoveryContext &ctx, const E &expression) {
  detail::ProbeRestoreScope guard{ctx};
  return attempt_parse_no_edits(ctx, expression);
}

} // namespace pegium::parser::detail
