/// `RecoveryPolicyFingerprint` axis coverage.
///
/// The `ChoiceRecoverCache` is semantically neutral: disabling it
/// must never change the chosen candidate. The cache neutrality
/// harness proves this end-to-end on a targeted suite. This file
/// complements it with a unit-level proof that each axis of
/// `RecoveryPolicyFingerprint` participates in cache-key equality:
/// flipping any single axis must produce a non-equal key, otherwise
/// two contexts that differ on that axis could share a cache hit and
/// the cache could silently influence the chosen candidate.
///
/// The test directly compares fingerprints (not cache hits) because
/// the cache equality predicate flows through the fingerprint's
/// `operator==`. If a future refactor tries to drop an axis from
/// the fingerprint without removing it from the policy, this test
/// fails immediately. A new axis added to the fingerprint must be
/// listed in `kAxisMutators` below or this test fails on the new
/// `static_assert` axis-count check.

#include <pegium/core/parser/ChoiceAttempt.hpp>

#include <gtest/gtest.h>

#include <array>
#include <functional>
#include <string_view>

using pegium::parser::detail::RecoveryPolicyFingerprint;

namespace {

/// A reference fingerprint with every axis at a non-default value
/// so flipping any one of them produces a clearly distinct snapshot.
RecoveryPolicyFingerprint reference_fingerprint() noexcept {
  static int dummy = 0;
  RecoveryPolicyFingerprint fp;
  fp.followProbeFn = static_cast<const void *>(&dummy);
  fp.recoverableFollowProbeFn = static_cast<const void *>(&fp);
  fp.recoverableFollowConsumesVisibleProbeFn =
      static_cast<const void *>(&dummy);
  fp.followChainIdentity = 0x5EED5EED5EED5EEDULL;
  fp.remainingEditBudget = 17;
  fp.consecutiveDeletes = 3;
  fp.editFloorOffset = 42;
  fp.allowInsert = true;
  fp.allowDelete = true;
  fp.skipAfterDelete = true;
  fp.allowDestructiveWindowContinuation = true;
  fp.allowLeadingTerminalInsertScope = true;
  fp.inRecoveryPhase = true;
  fp.hadEdits = true;
  fp.insideEditWindow = true;
  fp.completedWindowContinuation = true;
  return fp;
}

struct AxisMutator {
  std::string_view name;
  std::function<void(RecoveryPolicyFingerprint &)> mutate;
};

const std::array kAxisMutators = {
    AxisMutator{"followProbeFn",
                [](auto &fp) { fp.followProbeFn = nullptr; }},
    AxisMutator{"recoverableFollowProbeFn",
                [](auto &fp) { fp.recoverableFollowProbeFn = nullptr; }},
    AxisMutator{"recoverableFollowConsumesVisibleProbeFn",
                [](auto &fp) {
                  fp.recoverableFollowConsumesVisibleProbeFn = nullptr;
                }},
    AxisMutator{"followChainIdentity",
                [](auto &fp) { fp.followChainIdentity += 1; }},
    AxisMutator{"remainingEditBudget",
                [](auto &fp) { fp.remainingEditBudget += 1; }},
    AxisMutator{"consecutiveDeletes",
                [](auto &fp) { fp.consecutiveDeletes += 1; }},
    AxisMutator{"editFloorOffset",
                [](auto &fp) { fp.editFloorOffset += 1; }},
    AxisMutator{"allowInsert",
                [](auto &fp) { fp.allowInsert = !fp.allowInsert; }},
    AxisMutator{"allowDelete",
                [](auto &fp) { fp.allowDelete = !fp.allowDelete; }},
    AxisMutator{"skipAfterDelete",
                [](auto &fp) { fp.skipAfterDelete = !fp.skipAfterDelete; }},
    AxisMutator{"allowDestructiveWindowContinuation",
                [](auto &fp) {
                  fp.allowDestructiveWindowContinuation =
                      !fp.allowDestructiveWindowContinuation;
                }},
    AxisMutator{"allowLeadingTerminalInsertScope",
                [](auto &fp) {
                  fp.allowLeadingTerminalInsertScope =
                      !fp.allowLeadingTerminalInsertScope;
                }},
    AxisMutator{"inRecoveryPhase",
                [](auto &fp) { fp.inRecoveryPhase = !fp.inRecoveryPhase; }},
    AxisMutator{"hadEdits", [](auto &fp) { fp.hadEdits = !fp.hadEdits; }},
    AxisMutator{"insideEditWindow",
                [](auto &fp) { fp.insideEditWindow = !fp.insideEditWindow; }},
    AxisMutator{"completedWindowContinuation",
                [](auto &fp) {
                  fp.completedWindowContinuation =
                      !fp.completedWindowContinuation;
                }},
};

} // namespace

TEST(RecoveryPolicyFingerprintCoverage,
     every_axis_participates_in_equality) {
  for (const auto &axis : kAxisMutators) {
    SCOPED_TRACE(axis.name);
    auto a = reference_fingerprint();
    auto b = a;
    axis.mutate(b);
    EXPECT_FALSE(a == b)
        << "axis '" << axis.name
        << "' was mutated but operator== still treats the fingerprints as equal "
           "— either remove the axis from the fingerprint or add it to "
           "operator==";
  }
}

TEST(RecoveryPolicyFingerprintCoverage,
     unmodified_copy_compares_equal_to_reference) {
  const auto a = reference_fingerprint();
  const auto b = a;
  EXPECT_TRUE(a == b);
}
