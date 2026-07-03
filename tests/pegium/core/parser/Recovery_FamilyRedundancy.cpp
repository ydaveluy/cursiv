/// Family-redundancy filter invariants.
///
/// The family-redundancy filters (`extension_dominates` with its two guards,
/// and `boundary_repair_outranks_no_edit_iteration`) live as free functions in
/// `CandidateEnvelope.hpp`. They compare two candidates produced by the SAME
/// combinator's enumeration (parent/child dominance must not cross). That rule
/// is now STRUCTURAL at the call sites — `consider_choice_attempt` and
/// `consider_iteration_attempt` each build both envelopes from one producer —
/// so it is no longer a runtime `origin` axis (which is why the former
/// cross-origin / origin-matrix tests are gone).
///
/// This suite covers:
///   1. Each predicate returns true for canonical pairs that satisfy all
///      positive conditions (positive control).
///   2. The cost-vs-progress ratio guard and its carve-outs.

#include <pegium/core/parser/CandidateEnvelope.hpp>

#include <gtest/gtest.h>

#include <cstdint>

using pegium::parser::detail::boundary_repair_outranks_no_edit_iteration;
using pegium::parser::detail::CandidateEnvelope;
using pegium::parser::detail::extension_dominates;
using pegium::parser::detail::ExtensionDominanceGuard;
using pegium::parser::detail::ReplayPrefixClass;

namespace {

// -----------------------------------------------------------------------------
// Builders for canonical "outranks" pairs
// -----------------------------------------------------------------------------

/// Build a `(next, current)` pair that satisfies every positive condition of
/// the `WhenCurrentProgressedPastAnchor` extension dominance.
[[nodiscard]] std::pair<CandidateEnvelope, CandidateEnvelope>
make_extension_outranks_pair() noexcept {
  CandidateEnvelope next;
  next.key.matched = true;
  next.key.firstEditOffset = 5;
  next.key.editCost = 3;
  next.key.progressAfterEdits = 10;
  next.replayPrefix = ReplayPrefixClass::ExtendedCommittedPrefix;
  CandidateEnvelope current;
  current.key.matched = true;
  current.key.firstEditOffset = 5;
  current.key.editCost = 1;
  current.key.progressAfterEdits = 7;
  current.replayPrefix = ReplayPrefixClass::NewLocalPrefix;
  return {next, current};
}

/// Build a `(candidate, committed)` pair that satisfies every positive
/// condition of `boundary_repair_outranks_no_edit_iteration`.
[[nodiscard]] std::pair<CandidateEnvelope, CandidateEnvelope>
make_boundary_repair_outranks_pair() noexcept {
  CandidateEnvelope candidate;
  candidate.key.matched = true;
  candidate.key.firstEditOffset = 5;
  candidate.key.editCost = 2;
  candidate.key.progressAfterEdits = 12;
  candidate.replayPrefix = ReplayPrefixClass::NewLocalPrefix;
  CandidateEnvelope committed;
  committed.key.matched = true;
  committed.key.firstEditOffset = 5;
  committed.key.editCost = 0;
  committed.key.progressAfterEdits = 6;
  committed.replayPrefix = ReplayPrefixClass::Empty;
  return {candidate, committed};
}

/// Build a `(next, current)` pair that satisfies every positive condition of
/// the `WhenCurrentIsExtended` (destructive) extension dominance.
[[nodiscard]] std::pair<CandidateEnvelope, CandidateEnvelope>
make_destructive_extension_outranks_pair() noexcept {
  CandidateEnvelope next;
  next.key.matched = true;
  next.key.firstEditOffset = 8;
  next.key.editCost = 4;
  next.key.progressAfterEdits = 15;
  next.replayPrefix = ReplayPrefixClass::ExtendedCommittedPrefix;
  CandidateEnvelope current;
  current.key.matched = true;
  current.key.firstEditOffset = 8;
  current.key.editCost = 2;
  current.key.progressAfterEdits = 10;
  current.replayPrefix = ReplayPrefixClass::NewLocalPrefix;
  return {next, current};
}

} // namespace

// -----------------------------------------------------------------------------
// 1. Positive control: canonical pairs that satisfy all conditions
// -----------------------------------------------------------------------------

TEST(FamilyRedundancy, extension_outranks_returns_true_for_canonical_pair) {
  auto [next, current] = make_extension_outranks_pair();
  EXPECT_TRUE(extension_dominates(
      next, current, ExtensionDominanceGuard::WhenCurrentProgressedPastAnchor));
}

TEST(FamilyRedundancy, boundary_repair_returns_true_for_canonical_pair) {
  auto [candidate, committed] = make_boundary_repair_outranks_pair();
  EXPECT_TRUE(boundary_repair_outranks_no_edit_iteration(candidate, committed));
}

TEST(FamilyRedundancy,
     destructive_extension_returns_true_for_canonical_pair) {
  auto [next, current] = make_destructive_extension_outranks_pair();
  EXPECT_TRUE(extension_dominates(
      next, current, ExtensionDominanceGuard::WhenCurrentIsExtended));
}

// -----------------------------------------------------------------------------
// 2. Cost-vs-progress ratio guard
//
// Both extension-dominance guards gate the "extension" promotion behind a
// cost-vs-progress ratio: each extra unit of edit cost must buy at least one
// extra unit of progress before `next` is allowed to dominate `current` as an
// extension. Without this guard, a costly delete-cascade restart inflates a
// branch's "progress" by skipping past garbage, falsely dominating a sibling's
// cheap fuzzy-keyword repair.
// -----------------------------------------------------------------------------
TEST(FamilyRedundancyRatioGuard, ratio_gate_and_carve_outs) {
  // One `(next, current)` pair per row, fed through
  // `extension_dominates(next, current, guard)`. Each row is an independent
  // scenario; the scoped trace names it so a failure pinpoints the row.
  struct Row {
    const char *scenario;
    // next facts
    std::uint32_t nextFirstEditOffset;
    std::uint32_t nextEditCost;
    std::uint32_t nextProgressAfterEdits;
    ReplayPrefixClass nextReplayPrefix;
    // current facts
    std::uint32_t currentFirstEditOffset;
    std::uint32_t currentEditCost;
    std::uint32_t currentProgressAfterEdits;
    ReplayPrefixClass currentReplayPrefix;
    // predicate selection + expectation
    ExtensionDominanceGuard guard;
    bool expected;
  };

  static constexpr Row kRows[] = {
      // Costly delete-cascade restart (cost 32, +12 progress) must NOT dominate
      // a cheap fuzzy repair (cost 1) at the same anchor.
      {"extension_outranks_rejects_costly_extension_over_efficient_current",
       0, 32, 23, ReplayPrefixClass::ExtendedCommittedPrefix,
       0, 1, 11, ReplayPrefixClass::NewLocalPrefix,
       ExtensionDominanceGuard::WhenCurrentProgressedPastAnchor, false},

      // Extra cost buys at least equivalent extra progress -> promotion holds.
      {"extension_outranks_accepts_progress_justifies_extra_cost",
       0, 5, 14, ReplayPrefixClass::ExtendedCommittedPrefix,
       0, 1, 10, ReplayPrefixClass::NewLocalPrefix,
       ExtensionDominanceGuard::WhenCurrentProgressedPastAnchor, true},

      // `current` is insert-only with no progress past its edit -> ratio guard
      // does not apply; a destructive extension that skips garbage wins.
      {"extension_outranks_does_not_apply_ratio_to_insert_only_no_progress_current",
       16, 100, 50, ReplayPrefixClass::ExtendedCommittedPrefix,
       16, 3, 16 /* no advance past edit position */,
       ReplayPrefixClass::NewLocalPrefix,
       ExtensionDominanceGuard::WhenCurrentProgressedPastAnchor, true},

      // Repetition-side: both destructive, but `next` costs much more for
      // marginal extra progress -> rejected.
      {"destructive_extension_rejects_costly_extension_over_cheap_destructive_current",
       34, 110, 126, ReplayPrefixClass::ExtendedCommittedPrefix,
       34, 1, 71, ReplayPrefixClass::ExtendedCommittedPrefix,
       ExtensionDominanceGuard::WhenCurrentIsExtended, false},

      // Cost gap 4, progress gap 4: balanced ratio -> promotion holds.
      {"destructive_extension_accepts_when_ratio_is_balanced",
       10, 8, 18, ReplayPrefixClass::ExtendedCommittedPrefix,
       10, 4, 14, ReplayPrefixClass::ExtendedCommittedPrefix,
       ExtensionDominanceGuard::WhenCurrentIsExtended, true},

      // Repetition-side guard only fires when BOTH are destructive; a
      // non-destructive `current` (NewLocalPrefix) disables the ratio gate.
      {"destructive_extension_does_not_apply_ratio_to_non_destructive_current",
       0, 50, 60, ReplayPrefixClass::ExtendedCommittedPrefix,
       0, 2, 20, ReplayPrefixClass::NewLocalPrefix,
       ExtensionDominanceGuard::WhenCurrentIsExtended, true},
  };

  for (const auto &row : kRows) {
    SCOPED_TRACE(row.scenario);
    CandidateEnvelope next;
    next.key.matched = true;
    next.key.firstEditOffset = row.nextFirstEditOffset;
    next.key.editCost = row.nextEditCost;
    next.key.progressAfterEdits = row.nextProgressAfterEdits;
    next.replayPrefix = row.nextReplayPrefix;
    CandidateEnvelope current;
    current.key.matched = true;
    current.key.firstEditOffset = row.currentFirstEditOffset;
    current.key.editCost = row.currentEditCost;
    current.key.progressAfterEdits = row.currentProgressAfterEdits;
    current.replayPrefix = row.currentReplayPrefix;
    EXPECT_EQ(extension_dominates(next, current, row.guard), row.expected);
  }
}
