#include <pegium/core/parser/RecoveryCandidate.hpp>

namespace pegium::parser::detail {

ReplayPrefixClass classify_replay_prefix(bool hadEdits,
                                         bool hasDestructiveEdit) noexcept {
  using enum ReplayPrefixClass;
  if (!hadEdits) {
    return Empty;
  }
  if (hasDestructiveEdit) {
    return ExtendedCommittedPrefix;
  }
  return NewLocalPrefix;
}

bool continues_after_first_edit(
    const EditableRecoveryCandidate &candidate) noexcept {
  return candidate.firstEditOffset != std::numeric_limits<TextOffset>::max() &&
         candidate.postSkipCursorOffset > candidate.lastEditEndOffset;
}

// Lexicographic comparison on (rankAnchorOffset, editCost) is not enough:
// a high-cost candidate at a slightly later anchor can win over a
// cheaper, more reliable candidate at a slightly earlier anchor. The score
// applies a graduated penalty proportional to excess editCost so a candidate
// that paid for its anchor is only preferred when the gain dominates
// the cost spread. Intentionally non-monotone in `rankAnchorOffset` alone.
TextOffset recovery_key_first_edit_score(const RecoveryKey &key) noexcept {
  const auto penalty = monus(key.editCost, kFirstEditPenaltyStep) /
                       kFirstEditPenaltyStep;
  return monus(key.rankAnchorOffset, penalty);
}

bool is_better_recovery_key(const RecoveryKey &lhs,
                            const RecoveryKey &rhs) noexcept {
  if (lhs.fullMatch != rhs.fullMatch) {
    return lhs.fullMatch;
  }
  if (lhs.matched != rhs.matched) {
    return lhs.matched;
  }
  const auto lhsScore = recovery_key_first_edit_score(lhs);
  const auto rhsScore = recovery_key_first_edit_score(rhs);
  if (lhsScore != rhsScore) {
    return lhsScore > rhsScore;
  }
  // Operation-taxed edit cost (`editCost + W * editCount`). Lower wins.
  const auto lhsFaithfulness = faithfulness(lhs);
  const auto rhsFaithfulness = faithfulness(rhs);
  if (lhsFaithfulness != rhsFaithfulness) {
    return lhsFaithfulness < rhsFaithfulness;
  }
  return lhs.progressAfterEdits > rhs.progressAfterEdits;
}

RecoveryKey
terminal_recovery_key(const TerminalRecoveryCandidate &candidate) noexcept {
  // Both anchors stay at the sentinel: terminal candidates all repair the
  // same site, so axis 3 reduces to the cost penalty and the ladder ranks
  // on primaryRankCost.
  return {
      .matched = candidate.kind != TerminalRecoveryChoiceKind::None,
      .editCost = candidate.cost.primaryRankCost,
      .progressAfterEdits = static_cast<TextOffset>(candidate.consumed),
  };
}

RecoveryKey
editable_recovery_key(const EditableRecoveryCandidate &candidate) noexcept {
  // keyProgressOffset is producer-chosen: post-skip cursor for OrderedChoice /
  // Group, raw iteration cursor for Repetition (Invariant 2).
  const TextOffset effectiveEditOffset = effective_first_edit_offset(
      candidate.editCount != 0u, candidate.firstEditOffset,
      candidate.keyProgressOffset);
  // Local candidates rank on the plain effective first-edit offset: a
  // mid-descent slice is partial, so folding an insert-only slice to its
  // progress would turn sibling selection into a consume-more race that
  // rewards speculative fabrication. The insert-transparent fold applies at
  // the attempt level only (`recovery_attempt_key`), where scripts are
  // complete.
  return {
      .matched = candidate.matched,
      .firstEditOffset = effectiveEditOffset,
      .rankAnchorOffset = effectiveEditOffset,
      .editCost = candidate.editCost,
      .editCount = candidate.editCount,
      .progressAfterEdits = candidate.keyProgressOffset,
  };
}

} // namespace pegium::parser::detail
