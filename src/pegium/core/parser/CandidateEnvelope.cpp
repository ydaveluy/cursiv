#include <pegium/core/parser/CandidateEnvelope.hpp>

namespace pegium::parser::detail {

CandidateEnvelope
to_candidate_envelope(const EditableRecoveryCandidate &candidate) noexcept {
  return {
      .key = editable_recovery_key(candidate),
      .replayPrefix = candidate.replayPrefix,
  };
}

// Shared admission shape for the extension-dominance predicate (file-local; the
// only caller is extension_dominates below). True iff both candidates share the
// same anchor (`firstEditOffset`), `next` carries
// `ReplayPrefixClass::ExtendedCommittedPrefix`, `current` carries any non-empty
// replay prefix, and `next` strictly progresses further at no-worse cost.
// (The caller always compares two candidates from the SAME producer, so there
// is no cross-combinator origin check here — see CandidateEnvelope.hpp.)
static bool is_extending_anchor_pair(const CandidateEnvelope &next,
                                     const CandidateEnvelope &current) noexcept {
  return next.replayPrefix == ReplayPrefixClass::ExtendedCommittedPrefix &&
         current.replayPrefix != ReplayPrefixClass::Empty &&
         next.key.firstEditOffset == current.key.firstEditOffset &&
         next.key.editCost >= current.key.editCost &&
         next.key.progressAfterEdits > current.key.progressAfterEdits;
}

bool extension_dominates(const CandidateEnvelope &next,
                         const CandidateEnvelope &current,
                         ExtensionDominanceGuard guard) noexcept {
  if (!is_extending_anchor_pair(next, current)) {
    return false;
  }
  // Both former predicates accepted outright unless their guard condition held,
  // in which case the extension must pay its way (progress gained >= extra cost).
  if (const bool ratioGuardActive =
          guard == ExtensionDominanceGuard::WhenCurrentProgressedPastAnchor
              ? current.key.progressAfterEdits > current.key.firstEditOffset
              : current.replayPrefix ==
                    ReplayPrefixClass::ExtendedCommittedPrefix) {
    // monus() (saturating subtraction) keeps the non-underflow invariant local:
    // both gaps are guaranteed non-negative by is_extending_anchor_pair
    // (editCost >= editCost, progressAfterEdits > progressAfterEdits) above.
    const auto costGap = monus(next.key.editCost, current.key.editCost);
    const auto progressGap =
        monus(next.key.progressAfterEdits, current.key.progressAfterEdits);
    return progressGap >= costGap;
  }
  return true;
}

bool boundary_repair_outranks_no_edit_iteration(
    const CandidateEnvelope &candidate,
    const CandidateEnvelope &committed) noexcept {
  return candidate.replayPrefix != ReplayPrefixClass::Empty &&
         committed.replayPrefix == ReplayPrefixClass::Empty &&
         candidate.key.progressAfterEdits > committed.key.progressAfterEdits;
}

SelectionVerdict consider_candidate_envelope(const CandidateEnvelope &candidate,
                                             const CandidateEnvelope &incumbent,
                                             bool incumbentValid,
                                             FamilyRedundancyPolicy policy) noexcept {
  if (!candidate.key.matched) {
    return SelectionVerdict::Keep;
  }
  if (!incumbentValid) {
    return SelectionVerdict::Take;
  }
  switch (policy) {
  case FamilyRedundancyPolicy::ChoiceAnchorExtension:
    if (extension_dominates(candidate, incumbent,
                            ExtensionDominanceGuard::WhenCurrentProgressedPastAnchor)) {
      return SelectionVerdict::Take;
    }
    break;
  case FamilyRedundancyPolicy::IterationExtensionFamily:
    if (extension_dominates(candidate, incumbent,
                            ExtensionDominanceGuard::WhenCurrentIsExtended)) {
      return SelectionVerdict::Take;
    }
    if (extension_dominates(incumbent, candidate,
                            ExtensionDominanceGuard::WhenCurrentIsExtended)) {
      return SelectionVerdict::Keep;
    }
    if (boundary_repair_outranks_no_edit_iteration(candidate, incumbent)) {
      return SelectionVerdict::Take;
    }
    break;
  case FamilyRedundancyPolicy::RankOnly:
    break;
  }
  return is_better_recovery_key(candidate.key, incumbent.key)
             ? SelectionVerdict::Take
             : SelectionVerdict::Keep;
}

} // namespace pegium::parser::detail
