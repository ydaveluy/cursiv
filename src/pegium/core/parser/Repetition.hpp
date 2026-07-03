#pragma once

#include <array>
#include <concepts>
#include <limits>
#include <memory>
#include <optional>
#include <pegium/core/grammar/Assignment.hpp>
#include <pegium/core/grammar/DataTypeRule.hpp>
#include <pegium/core/grammar/Group.hpp>
#include <pegium/core/grammar/Literal.hpp>
#include <pegium/core/grammar/OrderedChoice.hpp>
#include <pegium/core/grammar/ParserRule.hpp>
#include <pegium/core/grammar/Repetition.hpp>
#include <pegium/core/grammar/TerminalRule.hpp>
#include <pegium/core/parser/CandidateEnvelope.hpp>
#include <pegium/core/parser/CompletionSupport.hpp>
#include <pegium/core/parser/EditableRecoverySupport.hpp>
#include <pegium/core/parser/ExpectContext.hpp>
#include <pegium/core/parser/ExpectFrontier.hpp>
#include <pegium/core/parser/ParseAttempt.hpp>
#include <pegium/core/parser/ParseContext.hpp>
#include <pegium/core/parser/ParseExpression.hpp>
#include <pegium/core/parser/ParseMode.hpp>
#include <pegium/core/parser/RecoveryCandidate.hpp>
#include <pegium/core/parser/RecoveryTrace.hpp>
#include <pegium/core/parser/RecoveryUtils.hpp>
#include <pegium/core/parser/SkipperBuilder.hpp>
#include <pegium/core/parser/SkipperWrapped.hpp>
#include <pegium/core/parser/StepTrace.hpp>
#include <pegium/core/parser/TerminalRecoverySupport.hpp>
#include <string>
#include <string_view>

namespace pegium::parser {

template <std::size_t min, std::size_t max, NonNullableExpression Element>
struct Repetition : grammar::Repetition {
  static constexpr bool nullable = min == 0;
  static constexpr bool isFailureSafe =
      min == 0 ||
      (min == 1 &&
       (max != 1 || std::remove_cvref_t<Element>::isFailureSafe));
  static_assert(!(min == 0 && max == 0),
                "A Repetition cannot have both min and max set to 0.");


  explicit constexpr Repetition(Element &&element)
    requires(!std::is_lvalue_reference_v<Element>)
      : _element(std::move(element)) {}

  explicit constexpr Repetition(Element element)
    requires(std::is_lvalue_reference_v<Element>)
      : _element(element) {}

  constexpr Repetition(Repetition &&) noexcept = default;
  constexpr Repetition(const Repetition &) = default;
  constexpr Repetition &operator=(Repetition &&) noexcept = default;
  constexpr Repetition &operator=(const Repetition &) = default;

  const AbstractElement *getElement() const noexcept override {
    return std::addressof(_element);
  }
  std::size_t getMin() const noexcept override { return min; }
  std::size_t getMax() const noexcept override { return max; }

private:
  friend struct detail::ParseAccess;
  friend struct detail::ProbeAccess;
  friend struct detail::InitAccess;

  static constexpr bool is_optional = (min == 0 && max == 1);
  static constexpr bool is_star =
      (min == 0 && max == std::numeric_limits<std::size_t>::max());
  static constexpr bool is_plus =
      (min == 1 && max == std::numeric_limits<std::size_t>::max());
  static constexpr bool is_fixed = (min == max && min > 0);

  struct ExpectIterationResult {
    bool matched = false;
    bool blocked = false;
    bool progressed = false;
  };

  template <ParseModeContext Context> bool parse_impl(Context &ctx) const {
    if constexpr (StrictParseModeContext<Context>) {
      return parse_strict_impl(ctx);
    } else if constexpr (RecoveryParseModeContext<Context>) {
      return parse_recovery_impl(ctx);
    } else {
      return parse_expect_impl(ctx);
    }
  }

  template <StrictParseModeContext Context>
  bool parse_strict_impl(Context &ctx) const {
    if constexpr (is_optional) {
      (void)attempt_parse_strict(ctx, _element);
      return true;
    } else if constexpr (is_star) {
      if (!attempt_parse_strict(ctx, _element)) {
        return true;
      }
      auto checkpointAfterItem = ctx.mark();
      while (true) {
        ctx.skip();
        if (!attempt_parse_strict(ctx, _element)) {
          ctx.rewind(checkpointAfterItem);
          return true;
        }
        checkpointAfterItem = ctx.mark();
      }
    } else if constexpr (is_plus) {
      if (!attempt_parse_strict(ctx, _element)) {
        return false;
      }
      auto checkpointAfterItem = ctx.mark();
      while (true) {
        ctx.skip();
        if (!attempt_parse_strict(ctx, _element)) {
          ctx.rewind(checkpointAfterItem);
          return true;
        }
        checkpointAfterItem = ctx.mark();
      }
    } else if constexpr (is_fixed) {
      if (!parse(_element, ctx)) {
        return false;
      }
      for (std::size_t repetitionIndex = 1; repetitionIndex < min;
           ++repetitionIndex) {
        ctx.skip();
        if (!parse(_element, ctx)) {
          return false;
        }
      }
      return true;
    } else if constexpr (min == 0) {
      if (!attempt_parse_strict(ctx, _element)) {
        return true;
      }
      auto checkpointAfterItem = ctx.mark();
      std::size_t repetitionCount = 1;
      for (; repetitionCount < max; ++repetitionCount) {
        ctx.skip();
        if (!attempt_parse_strict(ctx, _element)) {
          ctx.rewind(checkpointAfterItem);
          return true;
        }
        checkpointAfterItem = ctx.mark();
      }
      return true;
    } else {
      if (!attempt_parse_strict(ctx, _element)) {
        return false;
      }
      auto checkpointAfterItem = ctx.mark();
      std::size_t repetitionCount = 1;
      for (; repetitionCount < min; ++repetitionCount) {
        ctx.skip();
        if (!attempt_parse_strict(ctx, _element)) {
          ctx.rewind(checkpointAfterItem);
          return false;
        }
        checkpointAfterItem = ctx.mark();
      }

      for (; repetitionCount < max; ++repetitionCount) {
        ctx.skip();
        if (!attempt_parse_strict(ctx, _element)) {
          ctx.rewind(checkpointAfterItem);
          return true;
        }
        checkpointAfterItem = ctx.mark();
      }
      return true;
    }
  }

  /// Recovery-mode entry point, dispatched at compile time on the repetition
  /// shape (after the descent-inactive guard falls back to the strict path):
  ///   - is_optional (`Repetition<0,1>`): strict no-edit attempt first; on
  ///     failure rewind, then a fast attempt gated by `has_entry_recovery_signal`
  ///     (no signal and not started => skip the option), then one recovery
  ///     iteration, then skip-if the suffix stays recoverable without it.
  ///   - is_star (`Repetition<0,N>`): `parse_zero_min_repetition_recovery`.
  ///   - is_plus (`Repetition<1,N>`): one mandatory recovery iteration, then a
  ///     skip-between loop until an iteration fails (a blocked frontier either
  ///     stops cleanly after window progress or fails the rule).
  ///   - is_fixed (`min == max`): exactly `min` strict parses, no recovery.
  ///   - general (`min..max`): `min` mandatory strict parses, then the zero-min
  ///     recovery (if `min == 0`) or the plus-style skip-between loop up to max.
  /// The per-iteration plan choice lives in `try_recovery_iteration` ->
  /// `enumerate_iteration_plans`.
  bool parse_recovery_impl(RecoveryContext &ctx) const {
    if (ctx.recoveryDescentInactive()) {
      TrackedParseContext &strictCtx = ctx;
      return parse_strict_impl(strictCtx);
    }
    PEGIUM_STEP_TRACE_INC(detail::StepCounter::RepetitionRecoverCalls);
    if constexpr (is_optional) {
      const auto checkpoint = ctx.mark();
      const char *const savedFurthestExploredCursor =
          ctx.maxCursor();
      const auto noEditObservation = observe_no_edit_parse(ctx, _element);
      if (noEditObservation.matched) {
        return finalize_optional_strict_success(ctx, checkpoint,
                                                savedFurthestExploredCursor);
      }
      ctx.rewind(checkpoint);
      ctx.restoreMaxCursor(savedFurthestExploredCursor);
      PEGIUM_STEP_TRACE_INC(detail::StepCounter::RepetitionFastAttempts);
      const bool startedWithoutEdits = noEditObservation.startedWithoutEdits;
      PEGIUM_STEP_TRACE_INC(
          startedWithoutEdits ? detail::StepCounter::RepetitionFastSuccess
                              : detail::StepCounter::RepetitionFastFailures);
      if (!startedWithoutEdits && !has_entry_recovery_signal(ctx)) {
        return true;
      }
      if (try_recovery_iteration(ctx, /*skipBetweenIterations=*/false)) {
        return true;
      }
      ctx.rewind(checkpoint);
      ctx.bumpMaxCursor(savedFurthestExploredCursor);
      const bool suffixRecoverableWithoutOptional =
          is_locally_recoverable_after_iteration_skip(ctx);
      // A speculative entry-recovery signal alone must not force an optional
      // branch to fail. Even when the optional started strictly, keep it
      // skippable if abandoning it preserves a recoverable suffix.
      return !startedWithoutEdits || suffixRecoverableWithoutOptional;
    } else if constexpr (is_star) {
      PEGIUM_RECOVERY_TRACE("[repeat * rule] enter offset=",
                            ctx.cursorOffset());
      const bool matched = parse_zero_min_repetition_recovery(
          ctx, std::numeric_limits<std::size_t>::max());
      PEGIUM_RECOVERY_TRACE(
          matched ? "[repeat * rule] stop offset="
                  : "[repeat * rule] first element failed offset=",
          ctx.cursorOffset());
      return matched;
    } else if constexpr (is_plus) {
      if (!try_recovery_iteration(ctx, /*skipBetweenIterations=*/false)) {
        PEGIUM_RECOVERY_TRACE("[repeat + rule] first element failed offset=",
                              ctx.cursorOffset());
        return false;
      }
      PEGIUM_RECOVERY_TRACE("[repeat + rule] first element ok offset=",
                            ctx.cursorOffset());
      while (true) {
        if (!try_recovery_iteration(ctx, /*skipBetweenIterations=*/true)) {
          if (ctx.frontierBlocked()) {
            if (blocked_frontier_stops_cleanly_after_window_progress(ctx)) {
              ctx.clearFrontierBlock();
              break;
            }
            return false;
          }
          break;
        }
        PEGIUM_RECOVERY_TRACE("[repeat + rule] element matched offset=",
                              ctx.cursorOffset());
      }
      PEGIUM_RECOVERY_TRACE("[repeat + rule] stop offset=", ctx.cursorOffset());
      return true;
    } else if constexpr (is_fixed) {
      for (std::size_t repetitionIndex = 0; repetitionIndex < min;
           ++repetitionIndex) {
        if (repetitionIndex > 0) {
          ctx.skip();
        }
        if (!parse(_element, ctx)) {
          return false;
        }
      }
      return true;
    } else {
      std::size_t repetitionCount = 0;
      for (; repetitionCount < min; ++repetitionCount) {
        if (repetitionCount > 0) {
          ctx.skip();
        }
        if (!parse(_element, ctx)) {
          return false;
        }
      }
      if constexpr (min == 0) {
        return parse_zero_min_repetition_recovery(ctx, max);
      }
      for (; repetitionCount < max; ++repetitionCount) {
        if (!try_recovery_iteration(ctx, repetitionCount > 0)) {
          if (ctx.frontierBlocked()) {
            if (blocked_frontier_stops_cleanly_after_window_progress(ctx)) {
              ctx.clearFrontierBlock();
              break;
            }
            return false;
          }
          break;
        }
      }
      return true;
    }
  }

  bool parse_expect_impl(ExpectContext &ctx) const {
    if constexpr (is_optional) {
      if (const auto result = try_expect_iteration(ctx, /*skipBefore=*/false);
          result.matched && result.blocked) {
        ctx.clearFrontierBlock();
      }
      return true;
    } else if constexpr (is_star) {
      while (true) {
        const auto result = try_expect_iteration(ctx, /*skipBefore=*/false);
        if (!result.matched) {
          return true;
        }
        if (result.blocked) {
          ctx.clearFrontierBlock();
          return true;
        }
        if (!result.progressed) {
          return true;
        }
        ctx.skip();
      }
    } else if constexpr (is_plus) {
      const auto first = try_expect_iteration(ctx, /*skipBefore=*/false);
      if (!first.matched) {
        return false;
      }
      if (first.blocked) {
        return true;
      }
      while (true) {
        const auto result = try_expect_iteration(ctx, /*skipBefore=*/true);
        if (!result.matched) {
          return true;
        }
        if (result.blocked) {
          ctx.clearFrontierBlock();
          return true;
        }
        if (!result.progressed) {
          return true;
        }
      }
    } else {
      std::size_t repetitionCount = 0;
      for (; repetitionCount < min; ++repetitionCount) {
        if (repetitionCount > 0) {
          ctx.skip();
        }
        if (!parse(_element, ctx)) {
          return false;
        }
        if (ctx.frontierBlocked()) {
          return true;
        }
      }
      for (; repetitionCount < max; ++repetitionCount) {
        const auto result = try_expect_iteration(ctx, /*skipBefore=*/true);
        if (!result.matched) {
          return true;
        }
        if (result.blocked) {
          ctx.clearFrontierBlock();
          return true;
        }
        if (!result.progressed) {
          return true;
        }
      }
      return true;
    }
  }

public:
  bool probeRecoverable(RecoveryContext &ctx) const {
    return attempt_fast_probe(ctx, _element) ||
           probe_locally_recoverable(_element, ctx);
  }

  bool probeRecoverableAtEntry(RecoveryContext &ctx) const {
    if (attempt_fast_probe(ctx, _element)) {
      return true;
    }
    return probe_recoverable_at_entry(_element, ctx);
  }

  bool probeRecoverableAtEntryConsumesVisible(RecoveryContext &ctx) const {
    if (ctx.probeFollowAcceptsHere()) {
      return false;
    }
    if (attempt_fast_probe(ctx, _element)) {
      return true;
    }
    if (probe_recoverable_at_entry_consumes_visible(_element, ctx)) {
      return true;
    }
    return probe_resync_entry_consumes_visible(ctx);
  }

  void init_impl(AstReflectionInitContext &ctx) const {
    parser::init(_element, ctx);
    // Populate the shape-facts cache during the sequential init pass so it is
    // never written lazily during parsing: one parser instance is shared across
    // parallel document builds, and a first-touch write in shapeFacts() on this
    // mutable cache would be a data race. The facts are purely structural, so
    // computing them here yields the same result as the former lazy path.
    (void)shapeFacts();
  }

private:
  template <StrictParseModeContext Context>
  bool probe_impl(Context &ctx) const {
    return attempt_fast_probe(ctx, _element);
  }

  template <StrictParseModeContext Context>
  bool fast_probe_impl(Context &ctx) const {
    return attempt_fast_probe(ctx, _element);
  }

  enum class IterationReplayKind : std::uint8_t {
    NoInsert,
    InsertRetry,
    DeleteRetry,
  };

  struct IterationAttempt {
    // NB: the replay kind is authoritatively carried by the IterationReplayPlan
    // (winningPlan / plan), so it is intentionally not mirrored here.
    /// Replay reads this candidate's progress fields. Repetition uses the shared
    /// `EditableRecoveryCandidate` carrier with `keyProgressOffset` set to the
    /// RAW iteration cursor (Invariant 2). The decision-relevant
    /// `CandidateEnvelope` view (key, family-redundancy filters) is projected on
    /// demand in `consider_iteration_attempt` rather than mirrored here.
    detail::EditableRecoveryCandidate candidate{};
  };

  struct IterationObservation {
    IterationAttempt noInsertAttempt{};
    detail::RecoveryProbeProgress noInsertProbe{};
    bool noInsertProgressed = false;
    bool startedWithoutEdits = false;
    bool iterationStarted = false;
  };

  struct IterationReplayPlan {
    IterationReplayKind kind = IterationReplayKind::NoInsert;
    bool legal = false;
    bool allowInsert = false;
    bool allowDelete = false;
    bool allowDestructiveWindowContinuation = false;
  };

  using IterationPlanList = std::array<IterationReplayPlan, 4>;

  /// Who owns the next visible token at an iteration boundary (see
  /// `decide_iteration_plans`): `Local` = the repeated element may recover;
  /// `Parent` = the parent FOLLOW recovers + consumes visible at the same
  /// cursor, so a weak/synthetic local start must defer (BOTH retry flags are
  /// then forced false LAST).
  enum class BoundaryOwnership : std::uint8_t { Local, Parent };

  /// Layer-1 facts observed at an iteration boundary
  /// (`observe_iteration_signals`) and consumed by `decide_iteration_plans`.
  /// `stopCleanly` short-circuits the whole enumeration; when it is set the
  /// boundary-ownership probes are NOT run and `boundaryOwnership` stays
  /// `Local`.
  struct IterationSignals {
    bool stopCleanly = false;
    bool noInsertCandidateLegal = false;
    bool firstZeroMinIteration = false;
    bool retryContinuesStartedIteration = false;
    bool noInsertProgressed = false;
    bool iterationStarted = false;
    bool speculativeSyntheticSingleLeafRetryAfterRecoveredIteration = false;
    BoundaryOwnership boundaryOwnership = BoundaryOwnership::Local;
  };

  struct IterationSelectionResult {
    IterationAttempt bestAttempt{};
    IterationReplayPlan winningPlan{
        .kind = IterationReplayKind::NoInsert,
    };
  };

  /// Selects the better iteration attempt. Shape:
  /// `admission -> family-redundancy -> ranking`, mirroring
  /// `OrderedChoice::consider_choice_attempt`. The two
  /// family-redundancy filters
  /// (`extension_dominates` with the `WhenCurrentIsExtended` guard, and
  /// `boundary_repair_outranks_no_edit_iteration`) are checked
  /// before the central ranking; they are NOT
  /// replay-equivalence dominance — they remove redundancy inside
  /// the same-anchor extension family.
  static void consider_iteration_attempt(IterationSelectionResult &selection,
                                         const IterationAttempt &candidate,
                                         const IterationReplayPlan &plan) {
    if (!candidate.candidate.matched) {
      return;
    }
    const auto take = [&]() {
      selection.bestAttempt = candidate;
      selection.winningPlan = plan;
    };
    if (!selection.bestAttempt.candidate.matched) {
      take();
      return;
    }
    // The decision-relevant `CandidateEnvelope` is a pure projection of the
    // Project both sides here (both come from THIS Repetition's iteration
    // enumeration) instead of storing a mirror on every IterationAttempt. The
    // family-redundancy free functions only ever compare candidates from one
    // producer, so there is no cross-combinator origin axis to carry.
    const auto candidateEnvelope =
        detail::to_candidate_envelope(candidate.candidate);
    const auto bestEnvelope =
        detail::to_candidate_envelope(selection.bestAttempt.candidate);
    if (detail::extension_dominates(
            candidateEnvelope, bestEnvelope,
            detail::ExtensionDominanceGuard::WhenCurrentIsExtended)) {
      take();
      return;
    }
    if (detail::extension_dominates(
            bestEnvelope, candidateEnvelope,
            detail::ExtensionDominanceGuard::WhenCurrentIsExtended)) {
      return;
    }
    if (detail::boundary_repair_outranks_no_edit_iteration(candidateEnvelope,
                                                           bestEnvelope)) {
      take();
      return;
    }
    if (detail::is_better_recovery_key(candidateEnvelope.key,
                                       bestEnvelope.key)) {
      take();
    }
  }

  [[nodiscard]] static bool
  has_legal_iteration_retry_plan(const IterationPlanList &plans) noexcept {
    for (const auto &plan : plans) {
      if (plan.legal && plan.kind != IterationReplayKind::NoInsert) {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] static std::optional<std::size_t>
  single_legal_iteration_retry_plan_index(
      const IterationPlanList &plans) noexcept {
    std::optional<std::size_t> singleIndex;
    for (std::size_t i = 0; i < plans.size(); ++i) {
      if (const auto &plan = plans[i];
          !plan.legal || plan.kind == IterationReplayKind::NoInsert) {
        continue;
      }
      if (singleIndex.has_value()) {
        return std::nullopt;
      }
      singleIndex = i;
    }
    return singleIndex;
  }

  [[nodiscard]] detail::EditableRecoveryCandidate
  capture_iteration_candidate(
      RecoveryContext &ctx,
      const RecoveryContext::Checkpoint &iterationCheckpoint,
      std::size_t recoveryEditCountBefore) const {
    TextOffset firstEditOffset = std::numeric_limits<TextOffset>::max();
    TextOffset lastEditEndOffset = 0;
    const bool hadEdits = ctx.recoveryEditCount() > recoveryEditCountBefore;
    bool hasDestructiveEdit = false;
    if (hadEdits) {
      const auto editSummary = detail::summarize_edits_since(
          ctx.recoveryEditsView(), recoveryEditCountBefore);
      firstEditOffset = editSummary.firstEditBeginOffset;
      hasDestructiveEdit = editSummary.hasDestructiveEdit;
      lastEditEndOffset = editSummary.maxEndOffset;
    }
    // Repetition ranks on the RAW iteration cursor (Invariant 2): keyProgressOffset
    // is the raw cursor, while postSkipCursorOffset / lastEditEndOffset feed the
    // shared `continues_after_first_edit` continuation test.
    return detail::EditableRecoveryCandidate{
        .matched = true,
        .hasDestructiveEdit = hasDestructiveEdit,
        .postSkipCursorOffset = detail::post_skip_cursor_offset(ctx),
        .keyProgressOffset = ctx.cursorOffset(),
        .editCost = ctx.editCostDelta(iterationCheckpoint),
        .editCount = static_cast<std::uint32_t>(ctx.recoveryEditCount() -
                                                recoveryEditCountBefore),
        .lastEditEndOffset = lastEditEndOffset,
        .firstEditOffset = firstEditOffset,
        .replayPrefix = detail::classify_replay_prefix(
            hadEdits, hasDestructiveEdit),
    };
  }

  [[nodiscard]] static bool
  iteration_retry_is_weak(const IterationAttempt &attempt,
                          TextOffset parseStartOffset) noexcept {
    // Weak retries are boundary fabrications: the first edit happens at the
    // iteration entry and no visible continuation proves ownership. If the
    // first edit is later than the iteration entry, the element already
    // consumed source before repairing its tail (for example a missing close
    // delimiter at EOF), so it is a real started iteration and may outlive the
    // active window. The `editCount != 0` guard short-circuits before
    // `continues_after_first_edit` (which is only meaningful once an edit
    // exists).
    return attempt.candidate.matched && attempt.candidate.editCount != 0u &&
           !detail::continues_after_first_edit(attempt.candidate) &&
           attempt.candidate.firstEditOffset <= parseStartOffset;
  }

  [[nodiscard]] static bool
  has_visible_remainder_after_current_cursor(const RecoveryContext &ctx) {
    return ctx.skip_without_builder(ctx.cursor()) < ctx.end;
  }

  [[nodiscard]] bool
  probe_resync_entry_consumes_visible(RecoveryContext &ctx) const {
    const auto maxDeletes = is_optional ? ctx.maxConsecutiveCodepointDeletes
                                        : ctx.maxResyncSkipCodepoints;
    if (!ctx.allowDelete || maxDeletes == 0u || ctx.cursor() >= ctx.end) {
      return false;
    }
    detail::ScopedBoolOverride destructiveContinuationGuard{
        ctx.allowDestructiveWindowContinuation,
        ctx.allowDestructiveWindowContinuation || ctx.hasHadEdits()};
    (void)destructiveContinuationGuard;
    detail::ProbeRestoreScope guard{ctx};
    bool consumesVisible = false;
    (void)detail::visit_guarded_delete_scan_positions(
        ctx, [&ctx]() { return !ctx.probeFollowAcceptsHere(); },
        [this, &ctx, &consumesVisible](const detail::DeleteScanVisitState &) {
          using enum detail::DeleteScanVisitResult;
          if (ctx.probeFollowAcceptsHere()) {
            return Stop;
          }
          if (attempt_fast_probe(ctx, _element) ||
              probe_recoverable_at_entry_consumes_visible(_element, ctx)) {
            consumesVisible = true;
            return Accept;
          }
          return Continue;
        },
        {.scan = {.maxDeletes = maxDeletes,
                  .allowOverflow = !is_optional}});
    return consumesVisible;
  }

  [[nodiscard]] bool has_entry_recovery_signal(RecoveryContext &ctx) const {
    if (!has_visible_remainder_after_current_cursor(ctx)) {
      return false;
    }
    return probe_recoverable_at_entry(_element, ctx);
  }

  [[nodiscard]] bool
  is_locally_recoverable_after_iteration_skip(RecoveryContext &ctx) const {
    detail::ProbeRestoreScope guard{ctx};
    ctx.skip();
    return probe_locally_recoverable(_element, ctx);
  }

  /// Walks the grammar subtree starting at `element` and returns whether
  /// the *first non-nullable, non-wrapping leaf* satisfies `LeafPred`.
  /// Recursion shape:
  ///
  ///   - `Assignment` / `TerminalRule` / `DataTypeRule` / `ParserRule`
  ///     unwrap to their inner element;
  ///   - `Group`: skip nullable children, recurse into the first
  ///     non-nullable;
  ///   - `OrderedChoice`: every branch must satisfy `LeafPred`;
  ///   - `Repetition`: only consider it if `min > 0`, then recurse
  ///     into its element;
  ///   - everything else delegates to `LeafPred(element)`.
  ///
  /// Depth cap of 24 — grammars deeper than that are pathological and
  /// the walker returns `false`.
  template <typename LeafPred>
  [[nodiscard]] static bool walk_grammar_first_non_nullable_satisfies(
      const grammar::AbstractElement &element, LeafPred leafPred,
      std::uint8_t depth = 0) noexcept {
    if (depth > 24U) {
      return false;
    }
    const auto next = static_cast<std::uint8_t>(depth + 1U);
    switch (element.getKind()) {
    case ElementKind::Assignment:
      return walk_grammar_first_non_nullable_satisfies(
          *static_cast<const grammar::Assignment &>(element).getElement(),
          leafPred, next);
    case ElementKind::TerminalRule:
      return walk_grammar_first_non_nullable_satisfies(
          *static_cast<const grammar::TerminalRule &>(element).getElement(),
          leafPred, next);
    case ElementKind::DataTypeRule:
      return walk_grammar_first_non_nullable_satisfies(
          *static_cast<const grammar::DataTypeRule &>(element).getElement(),
          leafPred, next);
    case ElementKind::ParserRule:
      return walk_grammar_first_non_nullable_satisfies(
          *static_cast<const grammar::ParserRule &>(element).getElement(),
          leafPred, next);
    case ElementKind::Group: {
      const auto &group = static_cast<const grammar::Group &>(element);
      for (std::size_t i = 0; i < group.size(); ++i) {
        const auto &child = *group.get(i);
        if (child.isNullable()) {
          continue;
        }
        return walk_grammar_first_non_nullable_satisfies(child, leafPred,
                                                          next);
      }
      return false;
    }
    case ElementKind::OrderedChoice: {
      const auto &choice =
          static_cast<const grammar::OrderedChoice &>(element);
      if (choice.size() == 0U) {
        return false;
      }
      for (std::size_t i = 0; i < choice.size(); ++i) {
        if (!walk_grammar_first_non_nullable_satisfies(*choice.get(i),
                                                        leafPred, next)) {
          return false;
        }
      }
      return true;
    }
    case ElementKind::Repetition: {
      const auto &repetition =
          static_cast<const grammar::Repetition &>(element);
      return repetition.getMin() > 0U &&
             walk_grammar_first_non_nullable_satisfies(
                 *repetition.getElement(), leafPred, next);
    }
    default:
      return leafPred(element);
    }
  }

  [[nodiscard]] static bool starts_with_insertable_terminal(
      const grammar::AbstractElement &element) noexcept {
    return walk_grammar_first_non_nullable_satisfies(
        element, [](const grammar::AbstractElement &leaf) noexcept {
          if (leaf.getKind() != ElementKind::Literal) {
            return false;
          }
          const auto value =
              static_cast<const grammar::Literal &>(leaf).getValue();
          return detail::make_terminal_shape_from_literal(value).allowsInsert();
        });
  }

  [[nodiscard]] static bool
  is_unassigned_lexical_tail(const grammar::AbstractElement &element,
                             std::uint8_t depth = 0) noexcept {
    return walk_grammar_first_non_nullable_satisfies(
        element,
        [](const grammar::AbstractElement &leaf) noexcept {
          using enum ElementKind;
          switch (leaf.getKind()) {
          case TerminalRule:
          case AnyCharacter:
          case CharacterRange:
            return true;
          default:
            return false;
          }
        },
        depth);
  }

  [[nodiscard]] static bool
  starts_with_synthetic_terminal_then_unassigned_lexical_tail(
      const grammar::AbstractElement &element,
      std::uint8_t depth = 0) noexcept {
    if (depth > 24u) {
      return false;
    }
    switch (element.getKind()) {
    case ElementKind::DataTypeRule:
      return starts_with_synthetic_terminal_then_unassigned_lexical_tail(
          *static_cast<const grammar::DataTypeRule &>(element).getElement(),
          static_cast<std::uint8_t>(depth + 1u));
    case ElementKind::TerminalRule:
      return starts_with_synthetic_terminal_then_unassigned_lexical_tail(
          *static_cast<const grammar::TerminalRule &>(element).getElement(),
          static_cast<std::uint8_t>(depth + 1u));
    case ElementKind::Group: {
      const auto &group = static_cast<const grammar::Group &>(element);
      std::size_t i = 0;
      for (; i < group.size(); ++i) {
        const auto &child = *group.get(i);
        if (child.isNullable()) {
          continue;
        }
        if (!starts_with_insertable_terminal(child)) {
          return false;
        }
        ++i;
        break;
      }
      for (; i < group.size(); ++i) {
        const auto &child = *group.get(i);
        if (child.isNullable()) {
          continue;
        }
        return is_unassigned_lexical_tail(
            child, static_cast<std::uint8_t>(depth + 1u));
      }
      return false;
    }
    case ElementKind::Assignment:
    case ElementKind::ParserRule:
    case ElementKind::OrderedChoice:
    case ElementKind::Repetition:
    case ElementKind::Literal:
    case ElementKind::AndPredicate:
    case ElementKind::AnyCharacter:
    case ElementKind::CharacterRange:
    case ElementKind::Create:
    case ElementKind::InfixOperator:
    case ElementKind::InfixRule:
    case ElementKind::Nest:
    case ElementKind::NotPredicate:
    case ElementKind::UnorderedGroup:
      return false;
    }
    return false;
  }

  [[nodiscard]] static bool
  starts_with_required_literal_anchor(const grammar::AbstractElement &element,
                                      std::uint8_t depth = 0) noexcept {
    if (depth > 24u) {
      return false;
    }
    switch (element.getKind()) {
    case ElementKind::Literal:
      return !static_cast<const grammar::Literal &>(element).getValue().empty();
    case ElementKind::Assignment:
      return starts_with_required_literal_anchor(
          *static_cast<const grammar::Assignment &>(element).getElement(),
          static_cast<std::uint8_t>(depth + 1u));
    case ElementKind::TerminalRule:
      return starts_with_required_literal_anchor(
          *static_cast<const grammar::TerminalRule &>(element).getElement(),
          static_cast<std::uint8_t>(depth + 1u));
    case ElementKind::ParserRule:
      return starts_with_required_literal_anchor(
          *static_cast<const grammar::ParserRule &>(element).getElement(),
          static_cast<std::uint8_t>(depth + 1u));
    case ElementKind::DataTypeRule:
      return starts_with_required_literal_anchor(
          *static_cast<const grammar::DataTypeRule &>(element).getElement(),
          static_cast<std::uint8_t>(depth + 1u));
    case ElementKind::Group: {
      const auto &group = static_cast<const grammar::Group &>(element);
      for (std::size_t i = 0; i < group.size(); ++i) {
        const auto &child = *group.get(i);
        if (starts_with_required_literal_anchor(
                child, static_cast<std::uint8_t>(depth + 1u))) {
          return true;
        }
        if (!child.isNullable()) {
          return false;
        }
      }
      return false;
    }
    case ElementKind::OrderedChoice: {
      const auto &choice = static_cast<const grammar::OrderedChoice &>(element);
      if (choice.size() == 0u) {
        return false;
      }
      for (std::size_t i = 0; i < choice.size(); ++i) {
        if (!starts_with_required_literal_anchor(
                *choice.get(i), static_cast<std::uint8_t>(depth + 1u))) {
          return false;
        }
      }
      return true;
    }
    case ElementKind::Repetition: {
      const auto &repetition =
          static_cast<const grammar::Repetition &>(element);
      return repetition.getMin() > 0u &&
             starts_with_required_literal_anchor(
                 *repetition.getElement(),
                 static_cast<std::uint8_t>(depth + 1u));
    }
    case ElementKind::AndPredicate:
    case ElementKind::AnyCharacter:
    case ElementKind::CharacterRange:
    case ElementKind::Create:
    case ElementKind::InfixOperator:
    case ElementKind::InfixRule:
    case ElementKind::Nest:
    case ElementKind::NotPredicate:
    case ElementKind::UnorderedGroup:
      return false;
    }
    return false;
  }

  bool finalize_optional_strict_success(RecoveryContext &ctx,
                                        const RecoveryContext::Checkpoint &checkpoint,
    const char *savedFurthestExploredCursor) const {
    if (!has_visible_remainder_after_current_cursor(ctx)) {
      ctx.bumpMaxCursor(savedFurthestExploredCursor);
      return true;
    }

    const auto strictCursorOffset = ctx.cursorOffset();
    const auto strictExploredOffset =
        std::max(strictCursorOffset, ctx.maxCursorOffset());
    const auto strictPostSkipOffset = detail::post_skip_cursor_offset(ctx);
    const bool strictFrontierIsRecoveryRelevant =
        ctx.canEditAtOffset(strictPostSkipOffset) ||
        (ctx.hasPendingRecoveryWindows() &&
         strictPostSkipOffset >= ctx.pendingRecoveryWindowBeginOffset() &&
         strictPostSkipOffset <= ctx.pendingRecoveryWindowMaxCursorOffset());
    if (!strictFrontierIsRecoveryRelevant) {
      ctx.bumpMaxCursor(savedFurthestExploredCursor);
      return true;
    }

    ctx.rewind(checkpoint);
    ctx.restoreMaxCursor(savedFurthestExploredCursor);
    const auto baseEditCost = ctx.currentEditCost();
    const auto baseRecoveryEditCount = ctx.recoveryEditCount();
    if (baseRecoveryEditCount != 0u &&
        strictExploredOffset == strictCursorOffset) {
      const bool matchedStrictly = attempt_parse_no_edits(ctx, _element);
      ctx.bumpMaxCursor(savedFurthestExploredCursor);
      return matchedStrictly;
    }
    // A strictly matched optional already established a local boundary. Only
    // let the editable replay compete when it repairs the continuation inside
    // the local frontier already explored by the strict parse, instead of
    // reopening the already matched prefix or speculating deeper into the
    // remainder.
    const auto editableCandidate = detail::evaluate_editable_recovery_candidate(
        ctx, checkpoint, baseEditCost, baseRecoveryEditCount,
        [this, &ctx]() { return parse(_element, ctx); });
    const bool preferEditableMatch =
        editableCandidate.matched &&
        editableCandidate.editCount > 0u &&
        editableCandidate.firstEditOffset >= strictCursorOffset &&
        editableCandidate.firstEditOffset <= strictExploredOffset &&
        detail::continues_after_first_edit(editableCandidate) &&
        editableCandidate.postSkipCursorOffset > strictPostSkipOffset;
    if (preferEditableMatch) {
      return parse(_element, ctx);
    }

    const bool matchedStrictly = attempt_parse_no_edits(ctx, _element);
    ctx.bumpMaxCursor(savedFurthestExploredCursor);
    return matchedStrictly;
  }

  [[nodiscard]] static bool
  blocked_frontier_stops_cleanly_after_window_progress(
      const RecoveryContext &ctx) noexcept {
    return ctx.frontierBlocked() && ctx.hasHadEdits() &&
           ctx.hasPendingRecoveryWindows() &&
           ctx.maxCursorOffset() >=
               ctx.pendingRecoveryWindowMaxCursorOffset();
  }

  bool replay_iteration(RecoveryContext &ctx, const IterationReplayPlan &plan,
                        const char *parseStart,
                        bool allowTrailingDeleteStop = false) const {
    if (!plan.legal) {
      return false;
    }
    switch (plan.kind) {
    case IterationReplayKind::NoInsert:
      return attempt_parse_no_edits(ctx, _element) && ctx.cursor() != parseStart;
    case IterationReplayKind::InsertRetry: {
      auto editGuard =
          ctx.withEditPermissions(plan.allowInsert, plan.allowDelete);
      (void)editGuard;
      detail::ScopedBoolOverride destructiveContinuationGuard{
          ctx.allowDestructiveWindowContinuation,
          plan.allowDestructiveWindowContinuation ||
              ctx.allowDestructiveWindowContinuation};
      (void)destructiveContinuationGuard;
      return with_iteration_element_follow(ctx, [&]() {
        return parse(_element, ctx) && ctx.cursor() != parseStart;
      });
    }
    case IterationReplayKind::DeleteRetry: {
      auto editGuard =
          ctx.withEditPermissions(plan.allowInsert, plan.allowDelete);
      (void)editGuard;
      detail::ScopedBoolOverride destructiveContinuationGuard{
          ctx.allowDestructiveWindowContinuation,
          plan.allowDestructiveWindowContinuation ||
              ctx.allowDestructiveWindowContinuation};
      (void)destructiveContinuationGuard;
      const auto parseWithRetryPermissions = [this, &ctx, parseStart]() {
        return with_iteration_element_follow(ctx, [&]() {
          return parse(_element, ctx) && ctx.cursor() != parseStart;
        });
      };
      {
        detail::ProbeRestoreScope recoveryGuard{ctx};
        if (parseWithRetryPermissions()) {
          recoveryGuard.commit();
          return true;
        }
      }
      const bool canDeleteToEofAsTrailingGarbage =
          allowTrailingDeleteStop && ctx._followProbeFn == nullptr &&
          ctx._recoverableFollowProbeFn == nullptr;
      return detail::recover_by_guarded_delete_retry(
          ctx,
          [this, &ctx, canDeleteToEofAsTrailingGarbage](
              const detail::DeleteRetryVisitState &) {
            detail::ProbeRestoreScope guard{ctx};
            return (canDeleteToEofAsTrailingGarbage &&
                    ctx.cursor() == ctx.end) ||
                   attempt_fast_probe(ctx, _element) ||
                   probe_recoverable_at_entry(_element, ctx) ||
                   probe_locally_recoverable(_element, ctx);
          },
          [&ctx, &parseWithRetryPermissions,
           canDeleteToEofAsTrailingGarbage](const detail::DeleteRetryVisitState &) {
            detail::ProbeRestoreScope guard{ctx};
            const bool matched = parseWithRetryPermissions();
            const bool cleanedTrailingVisibleGarbage =
                canDeleteToEofAsTrailingGarbage && ctx.cursor() == ctx.end;
            if (matched) {
              guard.commit();
            }
            return matched || cleanedTrailingVisibleGarbage;
          },
          {.scan = {.allowOverflow = true},
           .stopOverflowAtStructuredVisibleSource = true});
    }
    }
    return false;
  }

  struct IterationElementFollowProbe {
    const Repetition *self = nullptr;
    RecoveryContext::FollowProbeFn outerStrict = nullptr;
    const void *outerStrictData = nullptr;
    RecoveryContext::FollowProbeFn outerRecoverable = nullptr;
    const void *outerRecoverableData = nullptr;
    RecoveryContext::FollowProbeFn outerRecoverableConsumesVisible = nullptr;
    const void *outerRecoverableConsumesVisibleData = nullptr;

    template <auto FnPtr, auto DataPtr>
    static bool dispatch_outer(RecoveryContext &ctx, const void *data) {
      const auto &probe =
          *static_cast<const IterationElementFollowProbe *>(data);
      if (attempt_fast_probe(ctx, probe.self->_element)) {
        return true;
      }
      auto fn = probe.*FnPtr;
      return fn != nullptr && fn(ctx, probe.*DataPtr);
    }

    static bool strict(RecoveryContext &ctx, const void *data) {
      return dispatch_outer<&IterationElementFollowProbe::outerStrict,
                            &IterationElementFollowProbe::outerStrictData>(ctx,
                                                                           data);
    }
    static bool recoverable(RecoveryContext &ctx, const void *data) {
      return dispatch_outer<&IterationElementFollowProbe::outerRecoverable,
                            &IterationElementFollowProbe::outerRecoverableData>(
          ctx, data);
    }
    static bool recoverableConsumesVisible(RecoveryContext &ctx,
                                           const void *data) {
      return dispatch_outer<
          &IterationElementFollowProbe::outerRecoverableConsumesVisible,
          &IterationElementFollowProbe::outerRecoverableConsumesVisibleData>(
          ctx, data);
    }
  };

  template <typename Fn>
  bool with_iteration_element_follow(RecoveryContext &ctx, Fn &&fn) const {
    IterationElementFollowProbe probe{
        .self = this,
        .outerStrict = ctx._followProbeFn,
        .outerStrictData = ctx._followProbeData,
        .outerRecoverable = ctx._recoverableFollowProbeFn,
        .outerRecoverableData = ctx._recoverableFollowProbeData,
        .outerRecoverableConsumesVisible =
            ctx._recoverableFollowConsumesVisibleProbeFn,
        .outerRecoverableConsumesVisibleData =
            ctx._recoverableFollowConsumesVisibleProbeData,
    };
    auto followGuard = ctx.withFollowProbe(
        &IterationElementFollowProbe::strict, &probe,
        &IterationElementFollowProbe::recoverable, &probe,
        &IterationElementFollowProbe::recoverableConsumesVisible, &probe);
    (void)followGuard;
    return std::forward<Fn>(fn)();
  }

  IterationObservation observe_iteration(
      RecoveryContext &ctx,
      const RecoveryContext::Checkpoint &iterationCheckpoint,
      const char *parseStart, TextOffset parseStartOffset) const {
    const auto visibleLeafCountBefore = ctx.failureHistorySize();
    const auto recoveryEditCountBefore = ctx.recoveryEditCount();
    IterationAttempt noInsertAttempt{};
    if (attempt_parse_no_edits(ctx, _element) && ctx.cursor() != parseStart) {
      noInsertAttempt.candidate = capture_iteration_candidate(
          ctx, iterationCheckpoint, recoveryEditCountBefore);
    }
    IterationObservation observation{
        .noInsertAttempt = noInsertAttempt,
        .noInsertProbe = detail::capture_recovery_probe_progress(
            ctx, visibleLeafCountBefore),
    };
    observation.noInsertProgressed =
        observation.noInsertProbe.committedProgressed(parseStartOffset);
    observation.startedWithoutEdits =
        !observation.noInsertProgressed &&
        probe_started_without_edits(ctx, _element);
    observation.iterationStarted =
        observation.noInsertProbe.exploredBeyond(parseStartOffset) ||
        observation.startedWithoutEdits;
    return observation;
  }

  /// Enumerates the iteration replay plans in two layers: observe the boundary
  /// facts (`observe_iteration_signals`), then decide the ordered legality
  /// verdicts (`decide_iteration_plans`). The clean stop short-circuits to no
  /// plans between the two.
  ///
  /// The `entryRecoveryConsumesVisible` probe is owned HERE so its memoized
  /// result is shared by both layers and fires at most once, only when a
  /// verdict actually needs it. The probe deliberately leaks `_maxCursor` (no
  /// ProbeRestoreScope) — the caller reads `ctx.maxCursor()` for furthest-error
  /// offsets — so WHEN it fires is observable: it must never become eager.
  [[nodiscard]] IterationPlanList enumerate_iteration_plans(
      RecoveryContext &ctx, bool skipBetweenIterations,
      const IterationObservation &observation, TextOffset parseStartOffset,
      std::size_t recoveryEditCountBefore,
      bool parseStartAtIterationEntryBoundary, bool firstStrictAccepts) const {
    std::optional<bool> entryRecoveryConsumesVisibleCache;
    const auto entryRecoveryConsumesVisible = [&]() {
      if (!entryRecoveryConsumesVisibleCache.has_value()) {
        detail::ScopedBoolOverride leadingInsertProbeGuard{
            ctx.allowLeadingTerminalInsertScope,
            ctx.allowLeadingTerminalInsertScope || !is_optional};
        (void)leadingInsertProbeGuard;
        entryRecoveryConsumesVisibleCache =
            probe_recoverable_at_entry_consumes_visible(_element, ctx);
      }
      return *entryRecoveryConsumesVisibleCache;
    };
    const IterationSignals sig = observe_iteration_signals(
        ctx, skipBetweenIterations, observation, parseStartOffset,
        recoveryEditCountBefore, parseStartAtIterationEntryBoundary,
        firstStrictAccepts, entryRecoveryConsumesVisible);
    if (sig.stopCleanly) {
      return {};
    }
    return decide_iteration_plans(ctx, sig, skipBetweenIterations,
                                  parseStartAtIterationEntryBoundary,
                                  entryRecoveryConsumesVisible);
  }

  /// Layer-1 of iteration-plan enumeration: observe the boundary facts.
  ///
  /// Most `IterationSignals` fields are eager scalars; the probe-triggering
  /// ones fire their (memoized) probes here via `&&` short-circuit, in the same
  /// order the monolithic enumerator used:
  ///   - `speculative...` may fire `entryRecoveryConsumesVisible`;
  ///   - the two boundary-ownership predicates may fire
  ///     `probeRecoverableFollowConsumesVisibleHere` and the un-memoized
  ///     `evaluate_editable_recovery_candidate` boundary probe.
  ///
  /// LOAD-BEARING: on the clean stop we return BEFORE the boundary-ownership
  /// predicates, so their probes do not fire then (exactly as the old early
  /// `return {}` did) and `boundaryOwnership` stays `Local`.
  template <class EntryRecoveryConsumesVisible>
  [[nodiscard]] IterationSignals observe_iteration_signals(
      RecoveryContext &ctx, bool skipBetweenIterations,
      const IterationObservation &observation, TextOffset parseStartOffset,
      std::size_t recoveryEditCountBefore,
      bool parseStartAtIterationEntryBoundary, bool firstStrictAccepts,
      EntryRecoveryConsumesVisible &&entryRecoveryConsumesVisible) const {
    IterationSignals sig;
    // The only boundary outcome the candidate enumeration consumes is the
    // clean stop: no local edit candidate, control returns to the parent.
    // It holds exactly when the body does not accept strictly at the cursor
    // (`firstStrict` is `noEditCandidate.matched`, reflecting FULL strict
    // acceptance — not the entry fast probe, which over-fires on a
    // multi-token body), the parent follow accepts strictly, and no
    // committed prefix forces the iteration to keep going.
    const auto &noEditCandidate = observation.noInsertAttempt.candidate;
    const bool followStrict = ctx.probeFollowAcceptsHere();
    const bool committedPrefixImposes = ctx.hasPendingCommittedRecoveryEdits();
    sig.stopCleanly =
        !noEditCandidate.matched && followStrict && !committedPrefixImposes;

    sig.noInsertCandidateLegal = observation.noInsertAttempt.candidate.matched &&
                                 observation.noInsertProgressed;
    sig.firstZeroMinIteration = min == 0 && !skipBetweenIterations;
    sig.retryContinuesStartedIteration =
        observation.noInsertProgressed || observation.iterationStarted;
    sig.noInsertProgressed = observation.noInsertProgressed;
    sig.iterationStarted = observation.iterationStarted;
    const auto hasEntryRecoveryRepairsBoundarySignal = [&]() {
      const auto probeCheckpoint = ctx.mark();
      detail::ScopedBoolOverride leadingInsertProbeGuard{
          ctx.allowLeadingTerminalInsertScope,
          ctx.allowLeadingTerminalInsertScope || !is_optional};
      (void)leadingInsertProbeGuard;
      const auto candidate = detail::evaluate_editable_recovery_candidate(
          ctx, probeCheckpoint, ctx.currentEditCost(),
          ctx.recoveryEditCount(), [this, &ctx, parseStartOffset]() {
            return with_iteration_element_follow(ctx, [&]() {
              return parse(_element, ctx) &&
                     ctx.cursorOffset() > parseStartOffset;
            });
          });
      return candidate.matched && candidate.editCount != 0u &&
             candidate.firstEditOffset == parseStartOffset &&
             candidate.postSkipCursorOffset > parseStartOffset;
    };
    sig.speculativeSyntheticSingleLeafRetryAfterRecoveredIteration =
        skipBetweenIterations &&
        parseStartAtIterationEntryBoundary &&
        ctx.hasHadEdits() &&
        !observation.startedWithoutEdits &&
        !observation.noInsertProgressed &&
        observation.noInsertProbe.exploredSingleVisibleLeafOrLess() &&
        !entryRecoveryConsumesVisible();
    // If the iteration started without prior edits, the enclosing Group's next
    // element already accepts the current cursor, and the NoInsert probe did
    // not progress, let iteration terminate cleanly instead of speculating
    // retry edits that would steal characters from the parent's follow. Edits
    // from outer recovery attempts must remain free to explore alternate
    // candidates, so gate only the ambient/clean entry point.
    const bool atLocalIterationBoundary =
        (skipBetweenIterations || min == 0u) &&
        parseStartAtIterationEntryBoundary;
    const auto &shape = shapeFacts();
    const bool localRecoveryBeginsWithLexicalChainTail =
        !firstStrictAccepts && shape.hasInsertableTerminal &&
        shape.hasSyntheticTerminalThenLexicalTail;
    // Lazy + memoized (no ScopedBoolOverride here — do not add one); fires only
    // when a boundary-ownership predicate reaches it.
    std::optional<bool> parentFollowRecoverableConsumesVisibleCache;
    const auto parentFollowRecoverableConsumesVisible = [&]() {
      if (!parentFollowRecoverableConsumesVisibleCache.has_value()) {
        parentFollowRecoverableConsumesVisibleCache =
            ctx.probeRecoverableFollowConsumesVisibleHere();
      }
      return *parentFollowRecoverableConsumesVisibleCache;
    };
    if (sig.stopCleanly) {
      return sig;
    }
    // A broad repeated element can consume one visible leaf and then fabricate
    // the rest of its shape, hiding a recoverable parent boundary. This remains
    // unsafe after earlier edits; a recoverable parent follow may only insert a
    // delimiter here, with visible consumption happening in the parent's next
    // site.
    // A nullable repetition boundary may see an element-shaped prefix that
    // fails before committing progress. If the parent can recover at the same
    // cursor and then consume visible input, the prefix is weaker evidence than
    // the parent boundary: local retry would reinterpret the next sibling as an
    // iteration item.
    const bool recoverableParentBoundaryOwnsWeakStartedRetry =
        atLocalIterationBoundary && !shape.hasRequiredLiteralAnchor &&
        observation.startedWithoutEdits && !observation.noInsertProgressed &&
        !followStrict && !committedPrefixImposes &&
        parentFollowRecoverableConsumesVisible() &&
        !hasEntryRecoveryRepairsBoundarySignal();
    // Same ownership rule for a fresh first iteration whose local recovery
    // begins with a lexical chain tail: the parent follow's visible
    // continuation is the stronger structural owner.
    const bool recoverableParentBoundaryOwnsFreshLexicalChainTail =
        atLocalIterationBoundary && recoveryEditCountBefore == 0u &&
        !ctx.hasHadEdits() && !observation.startedWithoutEdits &&
        !observation.noInsertProgressed &&
        localRecoveryBeginsWithLexicalChainTail &&
        !followStrict && !committedPrefixImposes &&
        parentFollowRecoverableConsumesVisible();
    // Who owns the next visible token at this boundary. Reads the two
    // already-evaluated predicates above — no probe re-fires.
    sig.boundaryOwnership =
        (recoverableParentBoundaryOwnsWeakStartedRetry ||
         recoverableParentBoundaryOwnsFreshLexicalChainTail)
            ? BoundaryOwnership::Parent
            : BoundaryOwnership::Local;
    return sig;
  }

  /// Layer-2 of iteration-plan enumeration: turn the observed signals into the
  /// four ordered replay plans, each marked `.legal`;
  /// `consider_iteration_attempt` does the central ranking (plan ORDER encodes
  /// the considered precedence, mirroring `OrderedChoice`).
  ///
  /// VERDICT SLOTS (precedence order):
  ///   [0] NoInsert                        — body matched strictly AND progressed.
  ///   [1] InsertRetry                     — synthesize the leading element, retry.
  ///   [2] DeleteRetry                     — delete noise, retry the started item.
  ///   [3] boundary-preserving InsertRetry — copy of [1] with allowDelete=false,
  ///       active only when [1].legal && [1].allowDelete && entry boundary.
  ///
  /// LEGALITY IS NOT AN `iff` PER PLAN — it is an ORDERED patch sequence whose
  /// final value depends on patch order (read top-to-bottom):
  ///   deleteRetryLegal: false
  ///     -> if(!noInsertCandidateLegal && !speculative)
  ///          iterationStarted || (skip && !hadEdits && probe_locally_recoverable)
  ///     -> if recoverableFirstZeroMinAfterPriorRecovery: true
  ///     -> if ownership==Parent: forced false.
  ///   insertRetryLegal: (noInsertCandidateLegal && allowInsert)
  ///     -> if false: = deleteRetryLegal
  ///     -> if speculative: forced false
  ///     -> three re-enable patches (firstZeroMin entry-signal; locally-
  ///        recoverable-after-skip; in-window entry-consumes-visible), each
  ///        guarded by !insertRetryLegal (so their probes fire only when needed)
  ///     -> if ownership==Parent: forced false.
  ///
  /// LOAD-BEARING INVARIANT (lets the two-point speculative application collapse
  /// to one force-false): firstZeroMinIteration requires !skipBetweenIterations
  /// while `speculative` requires skipBetweenIterations -> mutually exclusive.
  ///
  /// LAZY PROBES (`entryRecoveryConsumesVisible`, memoized via the orchestrator;
  /// `probe_locally_recoverable`, `has_entry_recovery_signal`,
  /// `is_locally_recoverable_after_iteration_skip`) fire ONLY on the patch that
  /// needs them, behind their `&&` short-circuit — they MUST stay lazy.
  template <class EntryRecoveryConsumesVisible>
  [[nodiscard]] IterationPlanList decide_iteration_plans(
      RecoveryContext &ctx, const IterationSignals &sig,
      bool skipBetweenIterations, bool parseStartAtIterationEntryBoundary,
      EntryRecoveryConsumesVisible &&entryRecoveryConsumesVisible) const {
    bool deleteRetryLegal = false;
    if (!sig.noInsertCandidateLegal &&
        !sig.speculativeSyntheticSingleLeafRetryAfterRecoveredIteration) {
      deleteRetryLegal = sig.iterationStarted ||
                         (skipBetweenIterations && !ctx.hasHadEdits() &&
                          probe_locally_recoverable(_element, ctx));
    }
    // A zero-min repetition is skippable, but not allowed to discard visible
    // source that clearly belongs to its first item after the parent already
    // opened the surrounding structure by recovery. Keep the item repair
    // admissible only when the entry probe proves visible consumption; pure
    // synthetic first items still stop cleanly.
    const bool recoverableFirstZeroMinAfterPriorRecovery =
        sig.firstZeroMinIteration && ctx.hasHadEdits() &&
        !sig.noInsertCandidateLegal && !sig.noInsertProgressed &&
        entryRecoveryConsumesVisible();
    if (!deleteRetryLegal && recoverableFirstZeroMinAfterPriorRecovery) {
      deleteRetryLegal = true;
    }
    bool insertRetryLegal = sig.noInsertCandidateLegal && ctx.allowInsert;
    if (!insertRetryLegal) {
      insertRetryLegal = deleteRetryLegal;
    }
    if (sig.speculativeSyntheticSingleLeafRetryAfterRecoveredIteration) {
      insertRetryLegal = false;
    }
    if (!insertRetryLegal && sig.firstZeroMinIteration &&
        !sig.noInsertCandidateLegal && !ctx.hasHadEdits()) {
      insertRetryLegal = has_entry_recovery_signal(ctx);
    }
    if (!insertRetryLegal && !sig.noInsertCandidateLegal && ctx.allowInsert &&
        !ctx.hasHadEdits()) {
      insertRetryLegal = is_locally_recoverable_after_iteration_skip(ctx);
    }
    if (!insertRetryLegal && skipBetweenIterations &&
        parseStartAtIterationEntryBoundary && ctx.hasHadEdits() &&
        !sig.noInsertProgressed && entryRecoveryConsumesVisible()) {
      insertRetryLegal = true;
    }
    if (sig.boundaryOwnership == BoundaryOwnership::Parent) {
      // The repeated element only proved a weak or synthetic local start while
      // the parent's follow can recover at the same boundary. Keep ownership
      // with the parent; later local fallback paths must not reopen retry
      // plans.
      deleteRetryLegal = false;
      insertRetryLegal = false;
    }
    // Lookahead gate for insert-into-option recovery.
    //
    // For a strictly optional repetition (`option(X)` = `Repetition<0,1>`),
    // the first iteration starts with no committed edits. If no clean
    // (no-insert) iteration is legal at this point, the parser has two
    // choices: skip the option (its `min=0` makes that legitimate), or
    // synthesize the iteration by inserting its leading element. The
    // insert opens a speculative path that frequently cascades into a
    // multi-edit fabrication (e.g. inserting `(` so a bare ID becomes a
    // `FunctionCall` with synthetic args, then needing more edits to
    // close the parens and patch the body). When the input has not even
    // started the option's body, the cleanest recovery is almost always
    // to skip — the surrounding rule will resync via its own recovery.
    //
    // Plan order encodes the considered precedence directly (the central
    // ranking still breaks ties): NoInsert, then InsertRetry, then
    // DeleteRetry, then the boundary-preserving insert. This reproduces the
    // order the former pre-evaluation pass imposed (insert before delete when
    // both are legal) without a separate scan.
    const bool destructiveWindowContinuation =
        ctx.hasHadEdits() && parseStartAtIterationEntryBoundary &&
        sig.retryContinuesStartedIteration;
    IterationPlanList plans{
        IterationReplayPlan{
            .kind = IterationReplayKind::NoInsert,
            .legal = sig.noInsertCandidateLegal,
        },
        IterationReplayPlan{
            .kind = IterationReplayKind::InsertRetry,
            .legal = insertRetryLegal,
            .allowInsert = true,
            .allowDelete = sig.retryContinuesStartedIteration && ctx.allowDelete,
            .allowDestructiveWindowContinuation = destructiveWindowContinuation},
        IterationReplayPlan{
            .kind = IterationReplayKind::DeleteRetry,
            .legal = deleteRetryLegal,
            .allowDelete = ctx.allowDelete,
            .allowDestructiveWindowContinuation = destructiveWindowContinuation},
        IterationReplayPlan{
            .kind = IterationReplayKind::InsertRetry,
        }};
    auto &insertRetryPlan = plans[1];
    auto &boundaryPreservingInsertRetryPlan = plans[3];
    if (insertRetryPlan.legal && insertRetryPlan.allowDelete &&
        parseStartAtIterationEntryBoundary) {
      boundaryPreservingInsertRetryPlan = insertRetryPlan;
      boundaryPreservingInsertRetryPlan.allowDelete = false;
    }
    // The clean-stop boundary (parent strict follow accepts and the
    // committed prefix does not impose) already returned no plans above.
    // The remaining boundary cases let the per-plan `legal` flags drive
    // the choice, except that `Resync` is gated only when the iteration is
    // OUTSIDE an active recovery window: inside an active window, the
    // dispatch's entry-recovery-signal fallback heuristics produce
    // legitimate retries a coarse boundary signal cannot distinguish from
    // speculative ones.
    return plans;
  }

  IterationAttempt evaluate_retry_iteration_attempt(
      RecoveryContext &ctx, const IterationReplayPlan &plan,
      const RecoveryContext::Checkpoint &iterationCheckpoint,
      const RecoveryContext::Checkpoint &parseCheckpoint,
      const char *parseStart, const char *parseStartFurthestExploredCursor,
      const char *&furthestExploredCursor,
      std::size_t recoveryEditCountBefore,
      bool skipBetweenIterations) const {
    IterationAttempt attempt{};
    const bool valid =
        replay_iteration(ctx, plan, parseStart,
                         skipBetweenIterations || min == 0);
    if (ctx.maxCursor() > furthestExploredCursor) {
      furthestExploredCursor = ctx.maxCursor();
    }
    if (valid) {
      attempt.candidate = capture_iteration_candidate(
          ctx, iterationCheckpoint, recoveryEditCountBefore);
    }
    ctx.rewind(parseCheckpoint);
    ctx.restoreMaxCursor(parseStartFurthestExploredCursor);
    return attempt;
  }

  IterationSelectionResult select_iteration_attempt(
      RecoveryContext &ctx, const IterationPlanList &plans,
      const IterationObservation &observation,
      const RecoveryContext::Checkpoint &iterationCheckpoint,
      const RecoveryContext::Checkpoint &parseCheckpoint,
      const char *parseStart, const char *parseStartFurthestExploredCursor,
      const char *&furthestExploredCursor,
      std::size_t recoveryEditCountBefore,
      bool skipBetweenIterations) const {
    IterationSelectionResult selection;
    // Plans are enumerated in considered-precedence order, so a single pass
    // evaluates each legal plan once; the central ranking breaks ties.
    for (std::size_t i = 0; i < plans.size(); ++i) {
      const auto &plan = plans[i];
      if (!plan.legal) {
        continue;
      }
      if (plan.kind == IterationReplayKind::NoInsert) {
        selection.bestAttempt = observation.noInsertAttempt;
        selection.winningPlan = plan;
        continue;
      }
      auto retryAttempt = evaluate_retry_iteration_attempt(
          ctx, plan, iterationCheckpoint, parseCheckpoint, parseStart,
          parseStartFurthestExploredCursor, furthestExploredCursor,
          recoveryEditCountBefore, skipBetweenIterations);
      consider_iteration_attempt(selection, retryAttempt, plan);
    }

    return selection;
  }

  /// Last-resort panic-mode resync skip. Pass 1 deletes noise until the
  /// iteration element itself re-parses cleanly (no destructive edit) from a
  /// fresh start. When `allowParentFollowFallback` is set, pass 2 (after
  /// rewinding to the entry) instead deletes until the parent follow accepts
  /// (or an implicit EOF follow). Both passes share one entry guard, one
  /// extended-delete-scan budget (the budget overrides config fields, not
  /// checkpoint state, so the inter-pass rewind leaves it enabled), and one
  /// restore epilogue.
  [[nodiscard]] bool try_resync_skip(RecoveryContext &ctx,
                                     const char *parseStart,
                                     TextOffset parseStartOffset,
                                     bool allowParentFollowFallback) const {
    if (!ctx.allowDelete || ctx.maxResyncSkipCodepoints == 0u ||
        ctx.cursor() != parseStart || ctx.cursor() >= ctx.end ||
        ctx.probeFollowAcceptsHere()) {
      return false;
    }
    // Do not reinterpret bytes inside a committed-prefix replay: the driver
    // already fixed the edit script for that region at an earlier site.
    if (ctx.hasPendingCommittedRecoveryEdits() &&
        parseStartOffset < ctx.committedRecoveryResumeFloor) {
      return false;
    }
    const auto recoveryCheckpoint = ctx.mark();
    const char *const savedFurthestExploredCursor = ctx.maxCursor();
    detail::ScopedBoolOverride destructiveContinuationGuard{
        ctx.allowDestructiveWindowContinuation,
        ctx.allowDestructiveWindowContinuation || ctx.hasHadEdits()};
    (void)destructiveContinuationGuard;
    detail::ExtendedDeleteScanBudgetScope<RecoveryContext> budgetScope{ctx};
    if (!budgetScope.tryEnable()) {
      return false;
    }
    const std::uint32_t codepointBudget = ctx.maxResyncSkipCodepoints;

    std::uint32_t deleted = 0u;
    while (deleted < codepointBudget && ctx.cursor() < ctx.end) {
      if (ctx.probeFollowAcceptsHere()) {
        break;
      }
      if (!ctx.deleteOneCodepoint()) {
        break;
      }
      ++deleted;
      // Cheap probe first: fast_probe short-circuits without exploring
      // recovery state. `probe_recoverable_at_entry` is the supercategory.
      // Mirrors the order in `probeRecoverableAtEntryConsumesVisible`.
      if (!attempt_fast_probe(ctx, _element) &&
          !probe_recoverable_at_entry(_element, ctx)) {
        continue;
      }
      const auto parseCheckpoint = ctx.mark();
      const char *const parseCursor = ctx.cursor();
      const auto targetRecoveryEditCount = ctx.recoveryEditCount();
      if ((attempt_parse_no_edits(ctx, _element) ||
           with_iteration_element_follow(
               ctx, [&]() { return parse(_element, ctx); })) &&
          ctx.cursor() != parseCursor) {
        if (const auto editSummary = detail::summarize_edits_since(
                ctx.recoveryEditsView(), targetRecoveryEditCount);
            editSummary.hasDestructiveEdit) {
          ctx.rewind(parseCheckpoint);
          continue;
        }
        budgetScope.commitOverflowEdits();
        return true;
      }
      ctx.rewind(parseCheckpoint);
    }

    if (allowParentFollowFallback) {
      // Reset to the iteration entry so the parent-follow scan starts clean.
      ctx.rewind(recoveryCheckpoint);
      deleted = 0u;
      while (deleted < codepointBudget && ctx.cursor() < ctx.end) {
        if (!ctx.deleteOneCodepoint()) {
          break;
        }
        ++deleted;
        const bool reachedImplicitEofFollow =
            ctx.cursor() == ctx.end && ctx._followProbeFn == nullptr &&
            ctx._recoverableFollowProbeFn == nullptr;
        if (ctx.probeFollowAcceptsHere() || reachedImplicitEofFollow) {
          budgetScope.commitOverflowEdits();
          return true;
        }
      }
    }

    ctx.rewind(recoveryCheckpoint);
    ctx.bumpMaxCursor(savedFurthestExploredCursor);
    return false;
  }

  bool parse_zero_min_repetition_recovery(RecoveryContext &ctx,
                                          std::size_t maxIterationCount) const {
    const auto checkpoint = ctx.mark();
    const char *const savedFurthestExploredCursor =
        ctx.maxCursor();
    if (!try_recovery_iteration(ctx, /*skipBetweenIterations=*/false)) {
      if (ctx.frontierBlocked()) {
        ctx.rewind(checkpoint);
        ctx.bumpMaxCursor(savedFurthestExploredCursor);
        return false;
      }
      ctx.rewind(checkpoint);
      ctx.bumpMaxCursor(savedFurthestExploredCursor);
      // Zero-min repetitions may stop cleanly when a speculative entry repair
      // does not turn into a real first iteration.
      PEGIUM_STEP_TRACE_INC(detail::StepCounter::RepetitionFastAttempts);
      const bool startedWithoutEdits =
          probe_started_without_edits(ctx, _element);
      PEGIUM_STEP_TRACE_INC(startedWithoutEdits
                                ? detail::StepCounter::RepetitionFastSuccess
                                : detail::StepCounter::RepetitionFastFailures);
      return !startedWithoutEdits;
    }
    for (std::size_t matchedCount = 1u; matchedCount < maxIterationCount;
         ++matchedCount) {
      if (!try_recovery_iteration(ctx, /*skipBetweenIterations=*/true)) {
        break;
      }
    }
    if (ctx.frontierBlocked()) {
      if (blocked_frontier_stops_cleanly_after_window_progress(ctx)) {
        ctx.clearFrontierBlock();
        return true;
      }
      return false;
    }
    return true;
  }

  bool try_recovery_iteration(RecoveryContext &ctx,
                              bool skipBetweenIterations) const {
    const auto iterationCheckpoint = ctx.mark();
    const auto recoveryEditCountBefore = ctx.recoveryEditCount();
    if (skipBetweenIterations) {
      ctx.skip();
    }
    const auto parseCheckpoint = ctx.mark();
    const char *const parseStart = ctx.cursor();
    const char *const parseStartFurthestExploredCursor =
        ctx.maxCursor();
    const TextOffset parseStartOffset = ctx.cursorOffset();
    const bool parseStartAtIterationEntryBoundary =
        detail::post_skip_cursor_offset(ctx) == parseStartOffset;
    // Capture the cheap element strict probe at the parseStart cursor
    // (where the iteration would begin a fresh attempt). The recoverable
    // entry probe is deliberately lazy inside `enumerate_iteration_plans`:
    // it can parse deeply, while the boundary table needs it only when no
    // strict element/follow/started-iteration fact already decides the case.
    bool firstStrictAccepts = false;
    {
      detail::ProbeRestoreScope guard{ctx};
      firstStrictAccepts = attempt_fast_probe(ctx, _element);
    }
    const auto observation =
        observe_iteration(ctx, iterationCheckpoint, parseStart, parseStartOffset);
    const auto iterationPlans = enumerate_iteration_plans(
        ctx, skipBetweenIterations, observation, parseStartOffset,
        recoveryEditCountBefore,
        parseStartAtIterationEntryBoundary, firstStrictAccepts);
    const bool hasLegalRetryPlan =
        has_legal_iteration_retry_plan(iterationPlans);
    const char *furthestExploredCursor = ctx.maxCursor();
    // `furthestExploredCursor` is mutated by reference inside
    // `select_iteration_attempt`, so the bail lambda captures by reference to
    // observe the latest value at each call site.
    const auto bailToIterationEntry = [&] {
      ctx.rewind(iterationCheckpoint);
      ctx.bumpMaxCursor(furthestExploredCursor);
      return false;
    };
    if (!iterationPlans.front().legal) {
      const auto singleRetryIndex =
          single_legal_iteration_retry_plan_index(iterationPlans);
      if (singleRetryIndex.has_value()) {
        ctx.rewind(parseCheckpoint);
        ctx.restoreMaxCursor(parseStartFurthestExploredCursor);
        const auto &singlePlan = iterationPlans[*singleRetryIndex];
        if (replay_iteration(
                ctx, singlePlan, parseStart, skipBetweenIterations || min == 0)) {
          return true;
        }
      }
    }
    ctx.rewind(parseCheckpoint);
    ctx.restoreMaxCursor(parseStartFurthestExploredCursor);
    // No legal local attempt: the no-edit branch did not progress and no
    // retry plan is admissible, so select_iteration_attempt would evaluate
    // nothing and return an unmatched best. Skip it and fall through to the
    // shared resync fallback below. The insertable-terminal deferral applies
    // ONLY on this path — defer to the parent follow rather than resync-skip;
    // it must not leak into the matched-but-failed-selection path.
    const bool noLegalLocalAttempt =
        !observation.noInsertProgressed && !hasLegalRetryPlan;
    if (noLegalLocalAttempt && shapeFacts().hasInsertableTerminal &&
        (ctx.probeFollowAcceptsHere() || ctx.probeRecoverableFollowHere())) {
      return bailToIterationEntry();
    }
    const IterationSelectionResult selection =
        noLegalLocalAttempt
            ? IterationSelectionResult{}
            : select_iteration_attempt(
                  ctx, iterationPlans, observation, iterationCheckpoint,
                  parseCheckpoint, parseStart, parseStartFurthestExploredCursor,
                  furthestExploredCursor, recoveryEditCountBefore,
                  skipBetweenIterations);
    if (!selection.bestAttempt.candidate.matched) {
      if (try_resync_skip(
              ctx, parseStart, parseStartOffset,
              /*allowParentFollowFallback=*/skipBetweenIterations || min == 0u)) {
        return true;
      }
      return bailToIterationEntry();
    }
    const bool weakRetryEscapesActiveWindow =
        skipBetweenIterations && recoveryEditCountBefore > 0u &&
        iteration_retry_is_weak(selection.bestAttempt, parseStartOffset);
    if (weakRetryEscapesActiveWindow) {
      return bailToIterationEntry();
    }
    // Rebuild only the winning branch from the shared parse checkpoint instead
    // of keeping success checkpoints alive across competing recovery parses.
    ctx.rewind(parseCheckpoint);
    ctx.restoreMaxCursor(parseStartFurthestExploredCursor);
    const bool replayed =
        replay_iteration(ctx, selection.winningPlan, parseStart,
                         skipBetweenIterations || min == 0);
    if (!replayed) {
      return bailToIterationEntry();
    }
    return replayed;
  }

public:

  constexpr const char *terminal(const char *begin) const noexcept
    requires TerminalCapableExpression<Element>
  {
    // optional
    if constexpr (is_optional) {
      const char *matchEnd = _element.terminal(begin);
      return matchEnd != nullptr ? matchEnd : begin;
    }
    // zero or more
    else if constexpr (is_star) {
      const char *cursor = begin;
      while (true) {
        const char *matchEnd = _element.terminal(cursor);
        if (matchEnd == nullptr) {
          break;
        }
        cursor = matchEnd;
      }
      return cursor;
    }
    // one or more
    else if constexpr (is_plus) {

      const char *cursor = _element.terminal(begin);
      if (cursor == nullptr)
        return cursor;

      while (true) {
        const char *matchEnd = _element.terminal(cursor);
        if (matchEnd == nullptr) {
          break;
        }
        cursor = matchEnd;
      }
      return cursor;
    }
    // only min/max times
    else if constexpr (is_fixed) {
      const char *cursor = begin;
      for (std::size_t repetitionIndex = 0; repetitionIndex < min;
           ++repetitionIndex) {
        cursor = _element.terminal(cursor);
        if (cursor == nullptr) {
          return cursor;
        }
      }
      return cursor;
    }
    // other cases
    else {

      const char *cursor = begin;
      std::size_t repetitionCount = 0;
      for (; repetitionCount < min; ++repetitionCount) {
        cursor = _element.terminal(cursor);
        if (cursor == nullptr) {
          return cursor;
        }
      }

      for (; repetitionCount < max; ++repetitionCount) {
        const char *matchEnd = _element.terminal(cursor);
        if (matchEnd == nullptr) {
          break;
        }
        cursor = matchEnd;
      }

      return cursor;
    }
  }
  constexpr const char *terminal(const std::string &text) const noexcept
    requires TerminalCapableExpression<Element>
  {
    return terminal(text.c_str());
  }

  constexpr bool isNullable() const noexcept override {
    return nullable;
  }

  template <std::convertible_to<Skipper> LocalSkipper>
    requires std::copy_constructible<ExpressionHolder<Element>>
  auto skip(LocalSkipper &&localSkipper) const & {
    return with_skipper(std::forward<LocalSkipper>(localSkipper));
  }

  template <std::convertible_to<Skipper> LocalSkipper>
  auto skip(LocalSkipper &&localSkipper) && {
    return std::move(*this).with_skipper(std::forward<LocalSkipper>(localSkipper));
  }

  template <typename... SkipperParts>
    requires((... && (detail::IsHiddenRules_v<SkipperParts> ||
                     detail::IsIgnoredRules_v<SkipperParts>))) &&
            std::copy_constructible<ExpressionHolder<Element>>
  auto skip(SkipperParts &&...parts) const & {
    return with_skipper(parser::skip(std::forward<SkipperParts>(parts)...));
  }

  template <typename... SkipperParts>
    requires((... && (detail::IsHiddenRules_v<SkipperParts> ||
                     detail::IsIgnoredRules_v<SkipperParts>)))
  auto skip(SkipperParts &&...parts) && {
    return std::move(*this).with_skipper(
        parser::skip(std::forward<SkipperParts>(parts)...));
  }

  template <std::convertible_to<Skipper> LocalSkipper>
    requires std::copy_constructible<ExpressionHolder<Element>>
  auto with_skipper(LocalSkipper &&localSkipper) const & {
    return SkipperWrapped<Repetition<min, max, Element>>{
        *this, static_cast<Skipper>(std::forward<LocalSkipper>(localSkipper))};
  }

  template <std::convertible_to<Skipper> LocalSkipper>
  auto with_skipper(LocalSkipper &&localSkipper) && {
    return SkipperWrapped<Repetition<min, max, Element>>{
        std::move(*this),
        static_cast<Skipper>(std::forward<LocalSkipper>(localSkipper))};
  }

private:
  ExpressionHolder<Element> _element;

  /// Pure-grammar facts derived from `_element` once per Repetition
  /// instance, cached so each iteration boundary reads a single field.
  struct ShapeFacts {
    bool hasInsertableTerminal = false;
    bool hasRequiredLiteralAnchor = false;
    bool hasSyntheticTerminalThenLexicalTail = false;
  };

  mutable std::optional<ShapeFacts> _shapeFacts;

  [[nodiscard]] const ShapeFacts &shapeFacts() const noexcept {
    if (!_shapeFacts.has_value()) {
      ShapeFacts f;
      f.hasInsertableTerminal = starts_with_insertable_terminal(_element);
      f.hasRequiredLiteralAnchor =
          starts_with_required_literal_anchor(_element);
      f.hasSyntheticTerminalThenLexicalTail =
          starts_with_synthetic_terminal_then_unassigned_lexical_tail(
              _element);
      _shapeFacts = f;
    }
    return *_shapeFacts;
  }

  ExpectIterationResult try_expect_iteration(ExpectContext &ctx,
                                             bool skipBefore) const {
    const auto checkpoint = ctx.mark();
    if (skipBefore) {
      ctx.skip();
    }
    const auto parseCheckpoint = ctx.mark();
    if (!parse(_element, ctx)) {
      ctx.rewind(checkpoint);
      return {};
    }
    const auto frontier = capture_frontier_since(ctx, parseCheckpoint);
    return {
        .matched = true,
        .blocked = frontier.blocked,
        .progressed = ctx.cursor() != parseCheckpoint.cursor,
    };
  }

};

template <std::size_t min, std::size_t max, NonNullableExpression Element>
using RepetitionWithSkipper =
    SkipperWrapped<Repetition<min, max, Element>>;

/// Make the `element` optional (repeated zero or one)
/// @tparam Element the expression to repeat
/// @param element the element to be optional
/// @return a repetition of zero or one `element`.
template <NonNullableExpression Element>
constexpr auto option(Element &&element) {
  return Repetition<0, 1, Element>{std::forward<Element>(element)};
}

/// Repeat the `element` zero or more
/// @tparam Element the expression to repeat
/// @param element the element to be repeated
/// @return a repetition of zero or more `element`.
template <NonNullableExpression Element>
constexpr auto many(Element &&element) {
  return Repetition<0, std::numeric_limits<std::size_t>::max(), Element>{
      std::forward<Element>(element)};
}

/// Repeat the `element` one or more
/// @tparam Element the expression to repeat
/// @param element the element to be repeated
/// @return a repetition of one or more `element`.
template <NonNullableExpression Element>
constexpr auto some(Element &&element) {
  return Repetition<1, std::numeric_limits<std::size_t>::max(), Element>{
      std::forward<Element>(element)};
}

/// Repeat the `element` one or more using a `separator`:
/// `element (separator element)*`
/// @tparam Element the expression to repeat
/// @tparam Sep the expression to use as separator
/// @param element the element to be repeated
/// @param separator the separator to be used between elements
/// @return a repetition of one or more `element` with a `separator`.
template <NonNullableExpression Element, Expression Sep>
constexpr auto some(Element &&element, Sep &&separator) {
  return std::forward<Element>(element) +
         many(std::forward<Sep>(separator) + std::forward<Element>(element));
}

/// Repeat the `element` zero or more using a `separator`
/// `(element (separator element)*)?`
/// @tparam Element the expression to repeat
/// @tparam Sep the expression to use as separator
/// @param element the element to be repeated
/// @param separator the separator to be used between elements
/// @return a repetition of zero or more `element` with a `separator`.
template <NonNullableExpression Element, Expression Sep>
constexpr auto many(Element &&element, Sep &&separator) {
  return option(
      some(std::forward<Element>(element), std::forward<Sep>(separator)));
}

/// Repeat the `element` `count` times.
/// @tparam count the count of repetitions
/// @tparam Element the expression to repeat
/// @param element the elements to be repeated
/// @return a repetition of `count` `element`.
template <std::size_t count, NonNullableExpression Element>
constexpr auto repeat(Element &&element) {
  return Repetition<count, count, Element>{std::forward<Element>(element)};
}

/// Repeat the `element` between `min` and `max` times.
/// @tparam min the min number of occurence (inclusive)
/// @tparam max the max number of occurence (inclusive)
/// @tparam Element the expression to repeat
/// @param element the elements to be repeated
/// @return a repetition of `min` to `max` `element`.
template <std::size_t min, std::size_t max, NonNullableExpression Element>
constexpr auto repeat(Element &&element) {
  return Repetition<min, max, Element>{std::forward<Element>(element)};
}

} // namespace pegium::parser
