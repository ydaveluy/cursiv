/// Truth-table equivalence: `consider_candidate_envelope` (the shared
/// selection kernel declared in `CandidateEnvelope.hpp`) against the three
/// original, combinator-specific selection functions it stands in for:
///
///   - `consider_choice_attempt`               (ChoiceAttempt.cpp)
///   - `consider_iteration_attempt`            (RepetitionRecoveryEngine.cpp)
///   - `consider_sequence_recovery_candidate`  (SequenceRecoveryEngine.cpp)
///
/// This suite is the gate BEFORE any call site is rewired onto the kernel:
/// it must be green before `consider_choice_attempt` et al. are collapsed
/// into thin wrappers. Each of the three originals is reimplemented below,
/// body-for-body, as a free function operating on bare `CandidateEnvelope`s
/// plus a `bool incumbentValid` standing in for that function's own "is
/// there a champion yet" test (`bestAttempt.envelope.key.matched` in
/// ChoiceAttempt.cpp, `selection.bestCandidate.matched` in
/// RepetitionRecoveryEngine.cpp, `bestCandidate.matched` in
/// SequenceRecoveryEngine.cpp — all three are exactly `incumbent.key.matched`
/// at the real call sites, since only a `.matched` candidate is ever
/// admitted as champion). These reimplementations are the oracle: the test
/// asserts the kernel agrees with them, not the other way around.
///
/// Coverage: 1) one named pair per shape called out by the unification
/// (candidate/incumbent strictly better, tie, candidate-extends-anchor,
/// incumbent-extends-anchor, boundary-repair-eligible, no incumbent,
/// unmatched candidate); 2) an exhaustive sweep over a discretized grid of
/// `(matched, replayPrefix, firstEditOffset, editCost, editCount,
/// progressAfterEdits)` on both sides, crossed with `incumbentValid`, run
/// through all three `FamilyRedundancyPolicy` values.

#include <pegium/core/parser/CandidateEnvelope.hpp>
#include <pegium/core/parser/RecoveryCandidate.hpp>

#include <gtest/gtest.h>

#include <cstdint>

using pegium::parser::detail::boundary_repair_outranks_no_edit_iteration;
using pegium::parser::detail::CandidateEnvelope;
using pegium::parser::detail::consider_candidate_envelope;
using pegium::parser::detail::extension_dominates;
using pegium::parser::detail::ExtensionDominanceGuard;
using pegium::parser::detail::FamilyRedundancyPolicy;
using pegium::parser::detail::is_better_recovery_key;
using pegium::parser::detail::ReplayPrefixClass;
using pegium::parser::detail::SelectionVerdict;

namespace {

// -----------------------------------------------------------------------------
// Oracles: verbatim reimplementations of the three original selection
// functions, projected onto bare `CandidateEnvelope`s.
// -----------------------------------------------------------------------------

/// `ChoiceAttempt.cpp`'s `consider_choice_attempt`: admission -> one
/// directional `extension_dominates` (only tried once an incumbent exists)
/// -> ranking.
[[nodiscard]] SelectionVerdict
oracle_choice_attempt(const CandidateEnvelope &candidate,
                      const CandidateEnvelope &incumbent,
                      bool incumbentValid) noexcept {
  if (!candidate.key.matched) {
    return SelectionVerdict::Keep;
  }
  if (incumbentValid &&
      extension_dominates(
          candidate, incumbent,
          ExtensionDominanceGuard::WhenCurrentProgressedPastAnchor)) {
    return SelectionVerdict::Take;
  }
  if (!incumbentValid || is_better_recovery_key(candidate.key, incumbent.key)) {
    return SelectionVerdict::Take;
  }
  return SelectionVerdict::Keep;
}

/// `RepetitionRecoveryEngine.cpp`'s `consider_iteration_attempt`: admission
/// -> take-if-no-incumbent -> bidirectional `extension_dominates` (a take-arm
/// and a reject-arm) -> `boundary_repair_outranks_no_edit_iteration` ->
/// ranking.
[[nodiscard]] SelectionVerdict
oracle_iteration_attempt(const CandidateEnvelope &candidate,
                         const CandidateEnvelope &incumbent,
                         bool incumbentValid) noexcept {
  if (!candidate.key.matched) {
    return SelectionVerdict::Keep;
  }
  if (!incumbentValid) {
    return SelectionVerdict::Take;
  }
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
  if (is_better_recovery_key(candidate.key, incumbent.key)) {
    return SelectionVerdict::Take;
  }
  return SelectionVerdict::Keep;
}

/// `SequenceRecoveryEngine.cpp`'s `consider_sequence_recovery_candidate`:
/// admission -> ranking only, no family-redundancy step.
[[nodiscard]] SelectionVerdict
oracle_sequence_recovery_candidate(const CandidateEnvelope &candidate,
                                   const CandidateEnvelope &incumbent,
                                   bool incumbentValid) noexcept {
  if (!candidate.key.matched) {
    return SelectionVerdict::Keep;
  }
  if (!incumbentValid || is_better_recovery_key(candidate.key, incumbent.key)) {
    return SelectionVerdict::Take;
  }
  return SelectionVerdict::Keep;
}

/// Asserts the kernel agrees with each oracle, under its corresponding
/// policy, on one `(candidate, incumbent, incumbentValid)` triple.
void expect_kernel_matches_all_oracles(const CandidateEnvelope &candidate,
                                       const CandidateEnvelope &incumbent,
                                       bool incumbentValid) {
  EXPECT_EQ(consider_candidate_envelope(candidate, incumbent, incumbentValid,
                                        FamilyRedundancyPolicy::RankOnly),
            oracle_sequence_recovery_candidate(candidate, incumbent,
                                               incumbentValid));
  EXPECT_EQ(
      consider_candidate_envelope(
          candidate, incumbent, incumbentValid,
          FamilyRedundancyPolicy::ChoiceAnchorExtension),
      oracle_choice_attempt(candidate, incumbent, incumbentValid));
  EXPECT_EQ(consider_candidate_envelope(
                candidate, incumbent, incumbentValid,
                FamilyRedundancyPolicy::IterationExtensionFamily),
            oracle_iteration_attempt(candidate, incumbent, incumbentValid));
}

[[nodiscard]] CandidateEnvelope
make_envelope(bool matched, ReplayPrefixClass prefix,
             std::uint32_t firstEditOffset, std::uint32_t editCost,
             std::uint32_t editCount,
             std::uint32_t progressAfterEdits) noexcept {
  CandidateEnvelope envelope;
  envelope.key.matched = matched;
  envelope.key.firstEditOffset = firstEditOffset;
  envelope.key.editCost = editCost;
  envelope.key.editCount = editCount;
  envelope.key.progressAfterEdits = progressAfterEdits;
  envelope.replayPrefix = prefix;
  return envelope;
}

} // namespace

// -----------------------------------------------------------------------------
// 1. Named scenarios: one pair per shape called out by the unification plan.
// -----------------------------------------------------------------------------

TEST(SelectionKernelEquivalence, candidate_strictly_better_on_faithfulness) {
  const auto candidate =
      make_envelope(true, ReplayPrefixClass::NewLocalPrefix, 5, 1, 0, 10);
  const auto incumbent =
      make_envelope(true, ReplayPrefixClass::NewLocalPrefix, 5, 1, 2, 6);
  expect_kernel_matches_all_oracles(candidate, incumbent,
                                    /*incumbentValid=*/true);
}

TEST(SelectionKernelEquivalence, incumbent_strictly_better_on_faithfulness) {
  const auto candidate =
      make_envelope(true, ReplayPrefixClass::NewLocalPrefix, 5, 1, 2, 6);
  const auto incumbent =
      make_envelope(true, ReplayPrefixClass::NewLocalPrefix, 5, 1, 0, 10);
  expect_kernel_matches_all_oracles(candidate, incumbent,
                                    /*incumbentValid=*/true);
}

TEST(SelectionKernelEquivalence, tie_keeps_incumbent) {
  const auto candidate =
      make_envelope(true, ReplayPrefixClass::NewLocalPrefix, 5, 2, 1, 9);
  const auto incumbent = candidate;
  expect_kernel_matches_all_oracles(candidate, incumbent,
                                    /*incumbentValid=*/true);
}

TEST(SelectionKernelEquivalence, candidate_extends_incumbent_anchor) {
  // Same anchor (firstEditOffset); candidate carries
  // `ExtendedCommittedPrefix` over incumbent's `NewLocalPrefix` at no-worse
  // cost and strictly more progress — the shape `is_extending_anchor_pair`
  // (and therefore both `extension_dominates` guards) requires.
  const auto candidate = make_envelope(
      true, ReplayPrefixClass::ExtendedCommittedPrefix, 8, 4, 1, 15);
  const auto incumbent =
      make_envelope(true, ReplayPrefixClass::NewLocalPrefix, 8, 4, 1, 10);
  expect_kernel_matches_all_oracles(candidate, incumbent,
                                    /*incumbentValid=*/true);
}

TEST(SelectionKernelEquivalence, incumbent_extends_candidate_anchor) {
  // Reverse of the above: the INCUMBENT is the one carrying the extension.
  // OrderedChoice's one-directional check never looks at this direction (it
  // must fall through to ranking); Repetition's reject-arm short-circuits
  // straight to Keep.
  const auto candidate =
      make_envelope(true, ReplayPrefixClass::NewLocalPrefix, 8, 4, 1, 10);
  const auto incumbent = make_envelope(
      true, ReplayPrefixClass::ExtendedCommittedPrefix, 8, 4, 1, 15);
  expect_kernel_matches_all_oracles(candidate, incumbent,
                                    /*incumbentValid=*/true);
}

TEST(SelectionKernelEquivalence, boundary_repair_eligible_shape) {
  // Candidate carries a non-empty replay prefix and progresses further than
  // an incumbent that carries none: `boundary_repair_outranks_no_edit_iteration`
  // territory (Repetition only; Choice/Group fall through to ranking, which
  // favors the same, genuinely-further-progressed candidate here).
  const auto candidate =
      make_envelope(true, ReplayPrefixClass::NewLocalPrefix, 5, 2, 1, 12);
  const auto incumbent =
      make_envelope(true, ReplayPrefixClass::Empty, 5, 0, 0, 6);
  expect_kernel_matches_all_oracles(candidate, incumbent,
                                    /*incumbentValid=*/true);
}

TEST(SelectionKernelEquivalence, no_incumbent_always_takes_a_matched_candidate) {
  const auto candidate =
      make_envelope(true, ReplayPrefixClass::Empty, 3, 0, 0, 3);
  const CandidateEnvelope incumbent{};
  expect_kernel_matches_all_oracles(candidate, incumbent,
                                    /*incumbentValid=*/false);
}

TEST(SelectionKernelEquivalence, unmatched_candidate_is_always_rejected) {
  const auto candidate =
      make_envelope(false, ReplayPrefixClass::Empty, 3, 0, 0, 30);
  const auto incumbent =
      make_envelope(true, ReplayPrefixClass::Empty, 5, 5, 5, 1);
  expect_kernel_matches_all_oracles(candidate, incumbent,
                                    /*incumbentValid=*/true);
  expect_kernel_matches_all_oracles(candidate, incumbent,
                                    /*incumbentValid=*/false);
}

// -----------------------------------------------------------------------------
// 2. Exhaustive grid over the discretized field space, both sides, all three
//    policies.
// -----------------------------------------------------------------------------

TEST(SelectionKernelEquivalence, exhaustive_grid_matches_every_original_shape) {
  static constexpr bool kMatchedValues[] = {false, true};
  static constexpr ReplayPrefixClass kPrefixValues[] = {
      ReplayPrefixClass::Empty, ReplayPrefixClass::ExtendedCommittedPrefix,
      ReplayPrefixClass::NewLocalPrefix};
  static constexpr std::uint32_t kOffsetValues[] = {0, 5, 10};
  static constexpr std::uint32_t kCostValues[] = {0, 3};
  static constexpr std::uint32_t kCountValues[] = {0, 2};
  static constexpr std::uint32_t kProgressValues[] = {0, 6, 12};
  static constexpr bool kIncumbentValidValues[] = {false, true};

  std::uint64_t casesChecked = 0;
  for (const bool candidateMatched : kMatchedValues) {
    for (const auto candidatePrefix : kPrefixValues) {
      for (const auto candidateOffset : kOffsetValues) {
        for (const auto candidateCost : kCostValues) {
          for (const auto candidateCount : kCountValues) {
            for (const auto candidateProgress : kProgressValues) {
              const auto candidate = make_envelope(
                  candidateMatched, candidatePrefix, candidateOffset,
                  candidateCost, candidateCount, candidateProgress);
              for (const bool incumbentMatched : kMatchedValues) {
                for (const auto incumbentPrefix : kPrefixValues) {
                  for (const auto incumbentOffset : kOffsetValues) {
                    for (const auto incumbentCost : kCostValues) {
                      for (const auto incumbentCount : kCountValues) {
                        for (const auto incumbentProgress :
                             kProgressValues) {
                          const auto incumbent = make_envelope(
                              incumbentMatched, incumbentPrefix,
                              incumbentOffset, incumbentCost, incumbentCount,
                              incumbentProgress);
                          for (const bool incumbentValid :
                               kIncumbentValidValues) {
                            expect_kernel_matches_all_oracles(
                                candidate, incumbent, incumbentValid);
                            ++casesChecked;
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  RecordProperty("cases_checked", static_cast<int>(casesChecked));
  // Guards against the grid silently shrinking to a trivial size.
  EXPECT_GT(casesChecked, 10000u);
}
