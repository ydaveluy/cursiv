#pragma once

/// User-facing knobs controlling recovery and expectation search.

#include <cstdint>

#include <pegium/core/parser/RecoveryConstants.hpp>

namespace pegium::parser {
/// Heuristics and limits controlling recovery and expectation search.
struct ParseOptions {
  /// Preferred limit for one contiguous delete run during normal recovery.
  ///
  /// Delete-scan recovery may temporarily continue past this limit when the
  /// normal budget still allows more deletions and no earlier match was found.
  std::uint32_t maxConsecutiveCodepointDeletes =
      kDefaultMaxConsecutiveCodepointDeletes;

  /// Initial number of visible tokens included before the failure point.
  std::uint32_t recoveryWindowTokenCount = 16;

  /// Upper bound for the in-window forward-widening retry.
  ///
  /// When the first attempt at a failure site is rejected as non-credible,
  /// the recovery pass reruns the local pipeline with a larger forward
  /// window (up to this cap) before advancing to the next site. The
  /// cap must be at least `recoveryWindowTokenCount`.
  std::uint32_t maxRecoveryWindowTokenCount = 64;

  /// Maximum number of candidate attempts explored inside one recovery window.
  std::uint32_t maxRecoveryAttempts = 64;

  /// Maximum number of edit operations allowed while replaying one active
  /// recovery window.
  std::uint32_t maxRecoveryEditsPerAttempt = 64;

  /// Preferred cumulative edit-cost budget for one recovery attempt.
  ///
  /// Some delete-scan retries may briefly exceed this while searching for a
  /// later resynchronization point. Such attempts are only considered
  /// selectable when they keep a stable prefix before the first edit.
  std::uint32_t maxRecoveryEditCost = 256;

  /// Hard global safety valve on the total counted recovery attempt runs
  /// (post-hoc pruning probes are uncounted). Prevents pathological inputs or
  /// fuzzer cases from driving unbounded recovery work. The effective cap is
  /// `min(maxRecoveryAttempts, maxTotalRecoveryAttemptRuns)`.
  std::uint32_t maxTotalRecoveryAttemptRuns = 1024;

  /// Hard cap on the cumulative number of `ParserRule` recovery entries
  /// allowed within one recovery window. Bounds the speculative search tree
  /// when a pathological grammar shape (e.g. unclosed nested call
  /// expressions) would otherwise cause `evaluate_editable_recovery_candidate`
  /// to explore exponentially many branches. Reaching the cap makes the
  /// inner recovery fail fast so the outer driver falls back to a less
  /// ambitious candidate.
  std::uint32_t maxRecoveryRuleEntries = 12000;

  /// Number of strict visible leaves that must parse after a recovery edit
  /// before the edit is considered stable.
  std::uint32_t recoveryStabilityTokenCount =
      kDefaultRecoveryStabilityTokenCount;

  /// Maximum number of codepoints a `Repetition` may skip when no normal
  /// recovery plan can bridge the current iteration. Acts as the last-resort
  /// panic-mode budget: the iteration scans forward up to this many codepoints
  /// looking for a clean restart of its element, emitting one fused `Delete`
  /// on success. Set to `0` to disable the panic-mode candidate entirely.
  std::uint32_t maxResyncSkipCodepoints = kDefaultMaxResyncSkipCodepoints;

  // NB: the internal probe-perturbation axes (forbid the out-of-window fuzzy
  // fold / the whole-keyword fuzzy Replace) are deliberately NOT here — they are
  // not user options. They live on the internal `RecoveryAttemptSpec::probeAxes`
  // and are set only by the sibling probes in `try_recovery_window`.

  /// Enables recovery-aware parsing and expectation tracing after strict failure.
  bool recoveryEnabled = true;

  /// Test-only / diagnostic options. Nested under a dedicated
  /// substruct so production paths can ignore them and the test
  /// harness has an explicit address for them. These MUST NOT
  /// change the chosen recovery candidate; their only role is to
  /// give tests access to internal optimisations (e.g. caches).
  ///
  /// Production callers should leave this at default. The substruct
  /// is public because C++ has no friend-namespace mechanism that
  /// would let a test header mutate a private field cleanly; the
  /// `Diagnostics` name + documentation are the contract that
  /// production code does not touch them.
  struct Diagnostics {
    /// Disables the `ChoiceRecoverCache` used to memoize
    /// `OrderedChoice` recovery attempts. The cache is purely an
    /// optimisation; the cache-neutrality harness flips this to
    /// prove that disabling the cache never changes the chosen
    /// candidate. Production code MUST leave this at `false`.
    bool recoveryCacheDisabled = false;
  };
  Diagnostics diagnostics;
};

} // namespace pegium::parser
