#pragma once

/// Candidate structures and the unified recovery ranking key.
///
/// Comparison axes (in priority order):
///   1. `fullMatch`          (true wins)
///   2. `matched`            (true wins)
///   3. `firstEditOffset`    penalised by excess `editCost` so a
///                           later-but-expensive candidate does not
///                           silently outrank a cheaper one at a
///                           slightly earlier offset
///                           (`recovery_key_first_edit_score`)
///   4. `faithfulness`       (lower wins): the operation-taxed edit cost
///                           `editCost + kFirstEditPenaltyStep * editCount`.
///                           Fuses the former separate `editCost` and
///                           `editCount` rungs into one scalar so a
///                           fold that swaps one operation for a slightly
///                           cheaper pair loses to the parsimonious
///                           single-operation recovery, while a genuinely
///                           cheaper recovery (saving a whole operation)
///                           still wins (`faithfulness`)
///   5. `progressAfterEdits` (higher wins)

#include <cstddef>
#include <cstdint>
#include <limits>

#include <pegium/core/parser/RecoveryCost.hpp>
#include <pegium/core/syntax-tree/CstNode.hpp>

namespace pegium::parser::detail {

/// Class of a candidate's replay prefix relative to the committed prefix
/// already applied at the site. Necessary but never sufficient for
/// replay-equivalence. (Formerly in RecoveryContract.hpp, now next to the
/// classifier and the carriers that produce it.)
enum class ReplayPrefixClass : std::uint8_t {
  /// The candidate has no edits to replay.
  Empty,
  /// The candidate's replay prefix is the committed prefix plus strictly later
  /// additional edits.
  ExtendedCommittedPrefix,
  /// The candidate's replay prefix is a new local prefix, not derived from the
  /// committed one.
  NewLocalPrefix,
};

enum class TerminalRecoveryChoiceKind : std::uint8_t {
  None,
  Insert,
  Replace,
  DeleteScan,
};

struct TerminalRecoveryCandidate {
  TerminalRecoveryChoiceKind kind = TerminalRecoveryChoiceKind::None;
  RecoveryCost cost{};
  std::uint32_t distance = std::numeric_limits<std::uint32_t>::max();
  std::size_t consumed = 0;
};

struct EditableRecoveryCandidate {
  bool matched = false;
  // Cursor after one skipper pass from the matched position, used by
  // `continues_after_first_edit` to ignore progress that only comes from
  // trailing trivia.
  TextOffset postSkipCursorOffset = 0;
  // Progress offset the ranking key reads (progressAfterEdits + the no-edit
  // first-edit fallback). Producer-chosen: OrderedChoice/Group use the post-skip
  // cursor (== postSkipCursorOffset); Repetition uses the RAW iteration cursor
  // (Invariant 2: same-start continuing repairs rank on raw progress, not
  // post-skip).
  TextOffset keyProgressOffset = 0;
  std::uint32_t editCost = 0;
  std::uint32_t editCount = 0;
  /// End offset of the last edit (max over all edits' end offsets), set
  /// directly to the edit slice's `maxEndOffset`. `continues_after_first_edit`
  /// compares the post-skip cursor against this directly.
  TextOffset lastEditEndOffset = 0;
  TextOffset firstEditOffset = std::numeric_limits<TextOffset>::max();
  /// Closed classification of the candidate's replay prefix relative
  /// to the surrounding scope's committed prefix. Set at construction
  /// based on the edit shape (`classify_replay_prefix`);
  /// consumed by dominance predicates that require both candidates
  /// to belong to the same replay-prefix equivalence class.
  ReplayPrefixClass replayPrefix = ReplayPrefixClass::Empty;
};

/// Classifies a candidate's replay prefix from its observed edit
/// shape. Closed mapping:
///
///   - no edits                 -> `Empty`
///   - edits with at least one
///     destructive edit          -> `ExtendedCommittedPrefix`
///   - insert-only edits        -> `NewLocalPrefix`
///
/// `hasDestructiveEdit` is the Deleted-OR-Replaced signal: both kinds
/// of destructive edit commit to a strictly different replay prefix
/// and therefore belong to the same equivalence class. Editable
/// candidates derive `hadEdits` as `editCount != 0`.
[[nodiscard]] ReplayPrefixClass
classify_replay_prefix(bool hadEdits, bool hasDestructiveEdit) noexcept;

struct RecoveryProbeProgress {
  TextOffset committedOffset = 0;
  TextOffset furthestExploredOffset = 0;
  std::size_t exploredVisibleLeafCount = 0;

  [[nodiscard]] constexpr bool
  committedProgressed(TextOffset startOffset) const noexcept {
    return committedOffset > startOffset;
  }

  [[nodiscard]] constexpr bool
  exploredBeyond(TextOffset offset) const noexcept {
    return furthestExploredOffset > offset;
  }

  [[nodiscard]] constexpr bool
  exploredSingleVisibleLeafOrLess() const noexcept {
    return exploredVisibleLeafCount <= 1u;
  }
};

[[nodiscard]] bool
continues_after_first_edit(const EditableRecoveryCandidate &candidate) noexcept;

struct RecoveryKey {
  /// Highest-priority axis: a fullMatch attempt always beats a non-fullMatch
  /// one regardless of edit cost or offset. Local candidate constructors
  /// (`terminal_recovery_key`, `editable_recovery_key`) leave this `false` —
  /// only attempt-level callers (`recovery_attempt_key`) know whether a global
  /// fullMatch is in scope.
  bool fullMatch = false;
  bool matched = false;
  TextOffset firstEditOffset = std::numeric_limits<TextOffset>::max();
  std::uint32_t editCost = 0u;
  std::uint32_t editCount = 0u;
  TextOffset progressAfterEdits = 0;
};

/// Step size of the graduated first-edit-offset penalty in
/// `recovery_key_first_edit_score`, and the per-operation tax weight `W`
/// in `faithfulness`. This was historically written as
/// `default_edit_cost(Deleted)`, which silently coupled the ranking
/// heuristic to the cost table — retuning the Delete cost would rescale
/// these unrelated curves with no warning. It is kept numerically equal but
/// DECOUPLED via its own named constant; the `static_assert` pins the
/// current relationship so a future cost-table change forces a deliberate
/// decision (follow the cost, or keep the curve fixed).
inline constexpr std::uint32_t kFirstEditPenaltyStep = 4u;
static_assert(kFirstEditPenaltyStep ==
                  default_edit_cost(ParseDiagnosticKind::Deleted),
              "kFirstEditPenaltyStep is intentionally decoupled from the Delete "
              "edit cost; if the Delete cost changes, decide deliberately "
              "whether the first-edit penalty curve should follow before "
              "updating this constant or the assertion.");

/// Operation-taxed edit cost: the single fused scalar that axis 4 of the
/// recovery key compares on (lower wins). Adds a per-operation tax of
/// `W = kFirstEditPenaltyStep` (one Delete's worth) to the raw `editCost`
/// for every edit operation in the candidate's script:
///
///   faithfulness(key) = key.editCost + W * key.editCount
///
/// This fuses the former two separate rungs (`editCost` then `editCount`)
/// into one comparison. `W = Delete-cost` is load-bearing: a delete->replace
/// fold saves at most Delete-Replace = 2 < W of cost per *added* operation,
/// so the faithful single-delete recovery still wins on faithfulness; a
/// genuinely cheaper recovery saves at least one whole Delete = W of cost
/// per *dropped* operation, so it still wins. The count-dominant trap (a
/// many-tiny-edit recovery beating a cheaper few-edit one purely on count)
/// is avoided by construction because the tax is additive on top of cost,
/// never a standalone tiebreak above it.
///
/// `editCount` is bounded by `maxRecoveryEditsPerAttempt`, so the wide
/// (uint32) product cannot overflow for any realistic budget.
[[nodiscard]] constexpr std::uint32_t
faithfulness(const RecoveryKey &key) noexcept {
  return key.editCost + kFirstEditPenaltyStep * key.editCount;
}

/// `firstEditOffset` penalised by edit cost: the score that axis 3
/// of the recovery key actually compares on.
///
/// A multi-byte delete that only wins on a 2-3 byte offset advantage
/// should not beat a short insert; but a single-char delete (cost 4)
/// should keep its baseline ranking. So:
///   - threshold = cost of a single Delete (the canonical "real" edit);
///     anything cheaper pays no penalty;
///   - above the threshold, each extra Delete-equivalent of cost
///     subtracts one offset unit from the score.
[[nodiscard]] TextOffset
recovery_key_first_edit_score(const RecoveryKey &key) noexcept;

/// Folds the "no edit" case to the candidate's own forward progress so a
/// candidate with `firstEditOffset = UINT_MAX` does not unfairly win axis 3 of
/// the `RecoveryKey` over an insert-and-continue candidate that made real
/// progress. Single-sourced here because three key builders
/// (`editable_recovery_key`, `recovery_attempt_key`, and `InfixRule`'s
/// editable-tail selector) share this exact rule; the signal "did it edit?"
/// lives in different fields per candidate type, but the offset-folding
/// contract is identical.
[[nodiscard]] constexpr TextOffset
effective_first_edit_offset(bool hadEdits, TextOffset firstEditOffset,
                            TextOffset progressFallback) noexcept {
  return hadEdits ? firstEditOffset : progressFallback;
}

[[nodiscard]] bool is_better_recovery_key(const RecoveryKey &lhs,
                                          const RecoveryKey &rhs) noexcept;

/// Terminal candidates use primaryRankCost (logical edit distance + strategy
/// penalty) as the cost axis. budgetCost measures the raw span deleted/
/// inserted, which inflates for fuzzy keyword repair and would lose to a
/// cheap-looking synthetic-gap split even when the fuzzy repair is the better
/// single-token edit.
[[nodiscard]] RecoveryKey
terminal_recovery_key(const TerminalRecoveryCandidate &candidate) noexcept;

/// Treat "no edit" as an edit at the candidate's current progress. Otherwise
/// a candidate with firstEditOffset = UINT_MAX would always beat an
/// insert-and-continue candidate on axis 3, even when the latter made much
/// more real progress.
[[nodiscard]] RecoveryKey
editable_recovery_key(const EditableRecoveryCandidate &candidate) noexcept;

// All dispatches project onto `RecoveryKey` via the type-specific
// `*_recovery_key` helpers and call `is_better_recovery_key`
// directly. There is no per-candidate-type ranking wrapper.

} // namespace pegium::parser::detail
