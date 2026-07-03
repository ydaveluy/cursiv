#pragma once

/// Non-template recovery engine for `Repetition` combinators.
///
/// The per-iteration recovery machinery (observe / plan-enumeration / select /
/// replay / resync) is independent of the repeated element's type: it touches
/// the element only through parse/probe entry points and pure grammar-shape
/// facts. This engine hosts that machinery once, compiled in
/// RepetitionRecoveryEngine.cpp; each `Repetition<min, max, Element>`
/// instantiation materializes a small `RepetitionElementOps` table of
/// type-erased trampolines instead of stamping the whole engine per element
/// type.
///
/// The iteration-element FOLLOW-probe installation deliberately stays
/// templated inside Repetition.hpp (reached via `withElementFollow`): the
/// installed probe function identities feed the choice-cache policy
/// fingerprint, so they must remain per-instantiation symbols.

#include <cstddef>

#include <pegium/core/parser/ParseAttempt.hpp>
#include <pegium/core/parser/RepetitionShapeAnalysis.hpp>
#include <pegium/core/utils/FunctionRef.hpp>

namespace pegium::parser::detail {

/// Type-erased view of one Repetition's element and shape. `self` is the
/// Repetition instance; every trampoline downcasts it and forwards to the
/// corresponding element helper. `shape` points at the instance's eagerly
/// populated grammar-shape facts.
struct RepetitionElementOps {
  const void *self = nullptr;
  const ShapeFacts *shape = nullptr;
  /// `Repetition<0,1>` — selects the resync delete budget/overflow policy and
  /// the leading-terminal-insert probe scope.
  bool isOptional = false;
  /// `min == 0` — a skippable repetition (zero-min boundary rules, and replay
  /// may stop on a trailing delete).
  bool minIsZero = false;

  bool (*parseNoEdits)(const void *self, RecoveryContext &ctx) = nullptr;
  bool (*parseEditable)(const void *self, RecoveryContext &ctx) = nullptr;
  bool (*fastProbe)(const void *self, RecoveryContext &ctx) = nullptr;
  bool (*probeLocallyRecoverable)(const void *self,
                                  RecoveryContext &ctx) = nullptr;
  bool (*probeAtEntry)(const void *self, RecoveryContext &ctx) = nullptr;
  bool (*probeAtEntryConsumesVisible)(const void *self,
                                      RecoveryContext &ctx) = nullptr;
  bool (*probeStartedWithoutEdits)(const void *self,
                                   RecoveryContext &ctx) = nullptr;
  /// Runs `body` with the iteration-element follow probes installed (the
  /// per-instantiation `IterationElementFollowProbe` machinery in
  /// Repetition.hpp).
  bool (*withElementFollow)(const void *self, RecoveryContext &ctx,
                            pegium::utils::function_ref<bool()> body) = nullptr;
};

/// One recovery iteration at the current cursor: observe the no-edit attempt,
/// enumerate the legal replay plans, select the best candidate, replay the
/// winner (with the panic-mode resync-skip as last resort).
[[nodiscard]] bool
repetition_try_recovery_iteration(RecoveryContext &ctx,
                                  const RepetitionElementOps &ops,
                                  bool skipBetweenIterations);

/// Recovery driver for a zero-min repetition (`many` / the `min == 0` general
/// shape): first iteration without inter-iteration skip, then iterations up to
/// `maxIterationCount`, with the clean-stop probe on a failed first iteration.
[[nodiscard]] bool
repetition_parse_zero_min_recovery(RecoveryContext &ctx,
                                   const RepetitionElementOps &ops,
                                   std::size_t maxIterationCount);

/// Post-processing of an optional (`Repetition<0,1>`) whose element matched
/// strictly: decides whether an editable replay that repairs the continuation
/// inside the already-explored frontier should outrank the strict match.
[[nodiscard]] bool repetition_finalize_optional_strict_success(
    RecoveryContext &ctx, const RepetitionElementOps &ops,
    const RecoveryContext::Checkpoint &checkpoint,
    const char *savedFurthestExploredCursor);

/// True when the element's entry recovery probe fires and visible input
/// remains after the cursor.
[[nodiscard]] bool
repetition_has_entry_recovery_signal(RecoveryContext &ctx,
                                     const RepetitionElementOps &ops);

/// Probes (state-restoring) whether the element stays locally recoverable
/// after skipping trivia from the current cursor.
[[nodiscard]] bool repetition_is_locally_recoverable_after_iteration_skip(
    RecoveryContext &ctx, const RepetitionElementOps &ops);

/// Entry-consumes-visible probe across a guarded delete scan: does deleting
/// noise reach a position where the element starts and consumes visible
/// input? Feeds `probeRecoverableAtEntryConsumesVisible`.
[[nodiscard]] bool
repetition_probe_resync_entry_consumes_visible(RecoveryContext &ctx,
                                               const RepetitionElementOps &ops);

/// A blocked frontier stops the iteration loop cleanly (instead of failing
/// the rule) once the active window's progress has been fully explored.
[[nodiscard]] bool repetition_blocked_frontier_stops_cleanly_after_window_progress(
    const RecoveryContext &ctx) noexcept;

} // namespace pegium::parser::detail
