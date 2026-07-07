#pragma once

/// Non-template recovery engine for `Group` (sequence) combinators.
///
/// The sequence-element recovery machinery (the missing-element insert
/// replay, the synthetic-terminal insert and delete-run attempts, the
/// skip-nullable / reparse-without-delete candidates and their selection by
/// the shared recovery key, and the current-/tail-failure recovery drivers)
/// touches the element pack only through parse/probe entry points on the
/// current element, whole-suffix probes, and pure grammar-shape scalars. This
/// engine hosts that machinery once, compiled in SequenceRecoveryEngine.cpp;
/// each `Group<Elements...>` instantiation materializes a small
/// `SequenceElementOps` table of type-erased trampolines per element boundary
/// instead of stamping the whole engine per (Elements..., I).
///
/// The four-transition driver (`parse_elements_recovery`), the
/// index-recursive probe walkers over the pack, and the
/// `with_current_follow_probe<I>` FOLLOW installation deliberately stay
/// templated inside Group.hpp: the installed follow-probe function identities
/// feed the choice-cache policy fingerprint, so they must remain
/// per-(Group, I) symbols. The engine reaches them through the
/// `withCurrentFollow`, suffix-probe and `parseTail` slots.

#include <cstdint>

#include <pegium/core/grammar/AbstractElement.hpp>
#include <pegium/core/parser/RecoveryContext.hpp>
#include <pegium/core/parser/TerminalRecoverySupport.hpp>
#include <pegium/core/utils/FunctionRef.hpp>

namespace pegium::parser::detail {

using grammar::ElementKind;

/// Entry facts of the sequence suffix at the current cursor: whether the
/// suffix accepts strictly / recoverably here. Produced by the
/// index-recursive `analyze_suffix_entry_facts` walker in Group.hpp and
/// consumed by the engine through the `analyzeSuffixEntryFacts` slot.
struct SequenceSuffixEntryFacts {
  bool strictStartsAtCurrentCursor = false;
  bool recoverableStartsAtCurrentCursor = false;
};

/// Terminal-recovery state observed at a sequence-element boundary before
/// the current element is parsed: the trivia-gap/prefix facts handed to the
/// element's terminal recovery, plus the sequence-local edit baseline used by
/// the terminal insert legality table.
struct TerminalRecoveryState {
  TerminalRecoveryFacts facts{};
  bool hasRecoveredPrefixBeforeCurrent = false;
  bool skippedFromRecoveryWindowBeforeCurrent = false;
  std::uint32_t sequenceEditCountBase = 0u;
};

/// Type-erased view of one Group element boundary. `self` is the Group
/// instance; every trampoline downcasts it and forwards to the corresponding
/// per-(Group, I) helper. The scalars are pure grammar-shape facts of the
/// current element / its suffix, evaluated once when the table is built on
/// the stack at each recovery descent.
struct SequenceElementOps {
  const void *self = nullptr;
  /// Address of the current element (`std::get<I>(elements)`), used for the
  /// synthetic-insert bookkeeping (`ctx.insertSynthetic` / `ctx.leaf`).
  const grammar::AbstractElement *current = nullptr;
  ElementKind currentKind{};
  bool currentNullable = false;
  /// `nullable_element_allows_recovery_skip<I>()` — false for Create and the
  /// predicate guards, which must not be bypassed by the skip transition.
  bool currentAllowsRecoverySkip = false;
  /// The previous element (if any) is a Literal or TerminalRule.
  bool previousIsTerminalish = false;
  /// `suffix_after_is_fully_nullable<I + 1>()`.
  bool suffixFullyNullable = false;
  /// `I == 0`.
  bool isFirstElement = false;

  bool (*parseCurrent)(const void *self, RecoveryContext &ctx) = nullptr;
  /// Parses the current element, preferring its terminal-recovery entry point
  /// (`parse_terminal_recovery_impl`) when the element provides one.
  bool (*parseCurrentTerminalRecovery)(const void *self, RecoveryContext &ctx,
                                       const TerminalRecoveryFacts &facts) =
      nullptr;
  bool (*parseCurrentWithoutSideEffects)(const void *self,
                                         RecoveryContext &ctx) = nullptr;
  bool (*fastProbeCurrent)(const void *self, RecoveryContext &ctx) = nullptr;
  bool (*probeCurrentAtEntry)(const void *self, RecoveryContext &ctx) = nullptr;
  bool (*probeCurrentLocallyRecoverable)(const void *self,
                                         RecoveryContext &ctx) = nullptr;
  bool (*probeCurrentAtEntryConsumesVisible)(const void *self,
                                             RecoveryContext &ctx) = nullptr;
  /// `allows_synthetic_terminal_insert(ctx, current, allowSequenceLiteralInsert)`
  /// (the per-element-type shape dispatch stays in Group.hpp).
  bool (*allowsSyntheticInsertCurrent)(const void *self, RecoveryContext &ctx,
                                       bool allowSequenceLiteralInsert) =
      nullptr;
  /// `current.getElement()` when the current element type exposes it,
  /// nullptr otherwise (feeds the missing-element insert admission).
  const grammar::AbstractElement *(*currentInnerElement)(const void *self) =
      nullptr;
  SequenceSuffixEntryFacts (*analyzeSuffixEntryFacts)(const void *self,
                                                      RecoveryContext &ctx) =
      nullptr;
  /// `walk_suffix_probes<detail::SuffixRecoverableAtEntryPolicy, I + 1>`.
  bool (*suffixProbeRecoverableEntry)(const void *self,
                                      RecoveryContext &ctx) = nullptr;
  /// `parse_elements<RecoveryContext, I + 1>(ctx, currentNullableConsumedVisible)`.
  bool (*parseTail)(const void *self, RecoveryContext &ctx,
                    bool currentNullableConsumedVisible) = nullptr;
  /// `parse_clean_tail_without_edits<I + 1>`.
  bool (*parseCleanTailNoEdits)(const void *self, RecoveryContext &ctx) =
      nullptr;
  /// `tail_supports_single_terminal_recovery<I + 1>()`.
  bool (*tailSupportsSingleTerminalRecovery)(const void *self) = nullptr;
  /// `first_required_tail_element<I + 1>()`.
  const grammar::AbstractElement *(*firstRequiredTailElement)(
      const void *self) = nullptr;
  /// Runs `body` with the current element's follow probes installed (the
  /// per-(Group, I) `with_current_follow_probe` machinery in Group.hpp).
  bool (*withCurrentFollow)(const void *self, RecoveryContext &ctx,
                            pegium::utils::function_ref<bool()> body) = nullptr;
};

/// InsertMissingCurrent admission: decides whether inserting the missing
/// (non-nullable, non-terminal-like) current element and replaying the tail
/// outranks parsing the current element in place.
[[nodiscard]] bool
sequence_select_missing_element_insert_replay(RecoveryContext &ctx,
                                              const SequenceElementOps &ops);

/// Replays the InsertMissingCurrent transition: synthesize the current
/// element, then a clean (edit-free) tail, falling back to a recoverable
/// tail when it supports single-terminal recovery.
bool sequence_replay_insert_missing_element_attempt(
    RecoveryContext &ctx, const SequenceElementOps &ops,
    bool &matchedCleanTail);

/// Observes the terminal-recovery state at the current boundary (trivia gap,
/// recovered/visible prefix, recovery-window skip, edit baseline).
[[nodiscard]] TerminalRecoveryState sequence_build_terminal_recovery_state(
    const RecoveryContext &ctx, const SequenceElementOps &ops,
    const RecoveryContext::Checkpoint &checkpoint, const char *cursorBeforeSkip,
    bool previousNullableSiblingOwnsCursor);

/// True when a nullable current element can recover by consuming visible
/// input at a cursor where the strict suffix already starts — the repair
/// then competes with the SkipNullable transition.
[[nodiscard]] bool
sequence_nullable_current_visible_repair_competes_with_strict_suffix(
    RecoveryContext &ctx, const SequenceElementOps &ops,
    bool strictSuffixStartsAtEntry);

/// Parses the nullable current element with no follow probe installed, so
/// its repair competes on its own merits against the strict suffix.
bool sequence_parse_competing_nullable_current_repair(
    RecoveryContext &ctx, const SequenceElementOps &ops,
    const TerminalRecoveryFacts &facts);

/// Encapsulates the `SkipNullable` transition. Returns `true` iff
/// the skip succeeded; returns `false` when the current is non-nullable,
/// already started visibly, the strict suffix entry trigger is not
/// available, or the skip attempt failed (in which case the cursor
/// was rewound).
[[nodiscard]] bool sequence_try_skip_nullable_transition(
    RecoveryContext &ctx, const SequenceElementOps &ops,
    bool nullableCurrentLooksStarted, bool strictSuffixStartsAtEntry);

/// Parses the current element with its follow probes installed.
bool sequence_parse_current_element(RecoveryContext &ctx,
                                    const SequenceElementOps &ops,
                                    const TerminalRecoveryFacts &facts);

/// RepairCurrent: enumerates the legal recovery attempts after the current
/// element failed to parse (terminal insert / insert-after-delete-run /
/// reparse-without-delete / skip-nullable), selects the best candidate and
/// replays the winner.
bool sequence_recover_after_current_parse_failure(
    RecoveryContext &ctx, const SequenceElementOps &ops,
    const RecoveryContext::Checkpoint &checkpoint,
    const TerminalRecoveryState &terminalRecoveryState,
    bool nullableCurrentLooksStarted);

/// RepairTail: after the current element matched but the tail failed —
/// retry the tail without deletes, then the current-failure enumeration
/// minus terminal attempts.
bool sequence_recover_after_tail_parse_failure(
    RecoveryContext &ctx, const SequenceElementOps &ops,
    const RecoveryContext::Checkpoint &checkpoint,
    const RecoveryContext::Checkpoint &checkpointAfterCurrent,
    const TerminalRecoveryFacts &terminalRecoveryFacts);

} // namespace pegium::parser::detail
