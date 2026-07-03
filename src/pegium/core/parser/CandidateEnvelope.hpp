#pragma once

/// `CandidateEnvelope`: the recovery candidate viewed through the
/// shared admission/dominance/ranking pipeline.
///
/// The envelope is the small common type that admission, dominance,
/// and ranking all read. It is intentionally minimal:
///
///   - `key`        — the existing `RecoveryKey` ranking tuple. The
///                    final preference order remains lexicographic on
///                    this key.
///   - `replayPrefix`— what the candidate observes about its replay
///                    position; a closed enum, no preference.
///
/// Strict-path discipline: this header is recovery-side. It must
/// never be constructed on the strict-only nominal path.

#include <cstdint>
#include <type_traits>

#include <pegium/core/parser/ContextShared.hpp>
#include <pegium/core/parser/RecoveryCandidate.hpp>
#include <pegium/core/utils/TextUtils.hpp>

namespace pegium::parser::detail {

/// The candidate envelope. Two axes:
///   - `key`: ranking. Read by `is_better_recovery_key`.
///   - `replayPrefix`: closed-enum structural observation. Read by the
///     family-redundancy predicates.
///
/// There is no `origin` axis: the family-redundancy predicates compare
/// candidates WITHIN one combinator's enumeration (parent/child dominance must
/// not cross), and every call site (consider_choice_attempt /
/// consider_iteration_attempt) already builds both envelopes from the same
/// producer — so the rule is structural at the call sites, not a runtime field.
struct CandidateEnvelope {
  RecoveryKey key{};
  ReplayPrefixClass replayPrefix = ReplayPrefixClass::Empty;
};

static_assert(std::is_trivially_copyable_v<CandidateEnvelope>,
              "CandidateEnvelope must remain trivially copyable so it can be "
              "moved cheaply through the pipeline (admission, dominance, "
              "ranking) without allocations.");

/// Maps an existing `EditableRecoveryCandidate` to the envelope. Both
/// `OrderedChoice`/`Group` and `Repetition` candidates now share this single
/// carrier; the producer-chosen `keyProgressOffset` already encodes the
/// per-combinator progress convention the ranking key reads.
[[nodiscard]] CandidateEnvelope
to_candidate_envelope(const EditableRecoveryCandidate &candidate) noexcept;

// -----------------------------------------------------------------------------
// Family-redundancy predicates (free functions)
// -----------------------------------------------------------------------------
//
// Dominance must not cross parent/child: these predicates compare two
// candidates within a single combinator's enumeration. A candidate
// produced by a parent scope must never be filtered by a candidate
// produced by a child scope (and vice-versa). That rule is enforced
// STRUCTURALLY at the call sites — `consider_choice_attempt` and
// `consider_iteration_attempt` only ever pass two candidates from the same
// producer — so there is no runtime origin axis to check here.
//
// The predicates are NOT replay-equivalence dominance: the two
// candidates carry different scripts. They are structural preferences
// inside a same-anchor extension family, applied as redundancy
// filters before the central `RecoveryKey` ranking sees the pair.


/// Selects which guard activates the cost/progress ratio test inside
/// `extension_dominates`. The two extension-dominance call sites share the
/// `is_extending_anchor_pair` + `progressGap >= costGap` body and differ ONLY in
/// when that ratio test applies.
enum class ExtensionDominanceGuard : std::uint8_t {
  /// `OrderedChoice`: apply the ratio test only once `current` has progressed
  /// strictly past its own anchor (`progressAfterEdits > firstEditOffset`).
  WhenCurrentProgressedPastAnchor,
  /// `Repetition` (destructive family): apply the ratio test only when `current`
  /// itself already carries an `ExtendedCommittedPrefix` replay prefix.
  WhenCurrentIsExtended,
};

/// `extension_dominates`: `next` outranks `current` when `next` carries
/// `ReplayPrefixClass::ExtendedCommittedPrefix` over the same anchor
/// (`firstEditOffset`) at no-worse cost and strictly further progress (see
/// `is_extending_anchor_pair`), with the selected `guard` deciding when the
/// `progressGap >= costGap` ratio test gates acceptance vs. accepting outright.
/// Unifies the former `extension_outranks_anchor_base`
/// (`WhenCurrentProgressedPastAnchor`) and `destructive_extension_outranks_anchor_base`
/// (`WhenCurrentIsExtended`).
[[nodiscard]] bool
extension_dominates(const CandidateEnvelope &next,
                    const CandidateEnvelope &current,
                    ExtensionDominanceGuard guard) noexcept;

/// `boundary_repair_outranks_no_edit_iteration`: `candidate`
/// outranks `committed` when `candidate` carries a non-empty
/// replay prefix, `committed` carries none, and `candidate`
/// progresses strictly further. Used by `Repetition` to prefer a
/// boundary repair over a trivial no-edit iteration.
[[nodiscard]] bool boundary_repair_outranks_no_edit_iteration(
    const CandidateEnvelope &candidate,
    const CandidateEnvelope &committed) noexcept;

// -----------------------------------------------------------------------------
// Selection kernel: admission -> family-redundancy -> ranking
// -----------------------------------------------------------------------------
//
// `OrderedChoice`, `Repetition`, and `Group` each select the better of two
// candidates through the same three-stage shape, differing only in which
// family-redundancy filters (if any) run between admission and ranking. The
// kernel below is the one shared shape; `FamilyRedundancyPolicy` selects the
// per-combinator middle stage.

/// Selects the per-combinator middle stage of `consider_candidate_envelope`.
enum class FamilyRedundancyPolicy : std::uint8_t {
  /// `Group`: no family-redundancy step, admission falls straight through to
  /// ranking (each plan is its own family).
  RankOnly,
  /// `OrderedChoice`: one-directional take-arm only, `extension_dominates`
  /// with `WhenCurrentProgressedPastAnchor`.
  ChoiceAnchorExtension,
  /// `Repetition`: bidirectional `extension_dominates` with
  /// `WhenCurrentIsExtended` (a take-arm AND a reject-arm), followed by
  /// `boundary_repair_outranks_no_edit_iteration`.
  IterationExtensionFamily,
};

/// The kernel's binary outcome: whether `candidate` replaces `incumbent`.
enum class SelectionVerdict : std::uint8_t { Keep, Take };

/// The shared admission -> family-redundancy -> ranking pipeline read by
/// `consider_choice_attempt`, `consider_iteration_attempt`, and
/// `consider_sequence_recovery_candidate`. `incumbentValid` stands in for
/// each call site's own "is there a candidate yet" test (`bestAttempt.
/// envelope.key.matched`, `selection.bestCandidate.matched`,
/// `bestCandidate.matched`) since a default-constructed `CandidateEnvelope`
/// is indistinguishable from a genuine unmatched candidate.
[[nodiscard]] SelectionVerdict
consider_candidate_envelope(const CandidateEnvelope &candidate,
                            const CandidateEnvelope &incumbent,
                            bool incumbentValid,
                            FamilyRedundancyPolicy policy) noexcept;

} // namespace pegium::parser::detail
