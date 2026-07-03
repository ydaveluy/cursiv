#pragma once

/// Out-of-class definitions of `Repetition`'s recovery-mode members declared
/// in Repetition.hpp: `parse_recovery_impl`, `element_ops`,
/// `probeRecoverable`, `probeRecoverableAtEntry`, and
/// `probeRecoverableAtEntryConsumesVisible`. A translation unit that drives a
/// Repetition through recovery mode must include this header in addition to
/// Repetition.hpp.

#include <pegium/core/parser/Repetition.hpp>

namespace pegium::parser {

template <std::size_t min, std::size_t max, NonNullableExpression Element>
bool Repetition<min, max, Element>::parse_recovery_impl(
    RecoveryContext &ctx) const {
  if (ctx.recoveryDescentInactive()) {
    TrackedParseContext &strictCtx = ctx;
    return parse_strict_impl(strictCtx);
  }
  PEGIUM_STEP_TRACE_INC(detail::StepCounter::RepetitionRecoverCalls);
  if constexpr (is_optional) {
    const auto ops = element_ops();
    const auto checkpoint = ctx.mark();
    const char *const savedFurthestExploredCursor = ctx.maxCursor();
    const auto noEditObservation = observe_no_edit_parse(ctx, _element);
    if (noEditObservation.matched) {
      return detail::repetition_finalize_optional_strict_success(
          ctx, ops, checkpoint, savedFurthestExploredCursor);
    }
    ctx.rewind(checkpoint);
    ctx.restoreMaxCursor(savedFurthestExploredCursor);
    PEGIUM_STEP_TRACE_INC(detail::StepCounter::RepetitionFastAttempts);
    const bool startedWithoutEdits = noEditObservation.startedWithoutEdits;
    PEGIUM_STEP_TRACE_INC(
        startedWithoutEdits ? detail::StepCounter::RepetitionFastSuccess
                            : detail::StepCounter::RepetitionFastFailures);
    if (!startedWithoutEdits &&
        !detail::repetition_has_entry_recovery_signal(ctx, ops)) {
      return true;
    }
    if (detail::repetition_try_recovery_iteration(
            ctx, ops, /*skipBetweenIterations=*/false)) {
      return true;
    }
    ctx.rewind(checkpoint);
    ctx.bumpMaxCursor(savedFurthestExploredCursor);
    const bool suffixRecoverableWithoutOptional =
        detail::repetition_is_locally_recoverable_after_iteration_skip(ctx,
                                                                       ops);
    // A speculative entry-recovery signal alone must not force an optional
    // branch to fail. Even when the optional started strictly, keep it
    // skippable if abandoning it preserves a recoverable suffix.
    return !startedWithoutEdits || suffixRecoverableWithoutOptional;
  } else if constexpr (is_star) {
    PEGIUM_RECOVERY_TRACE("[repeat * rule] enter offset=", ctx.cursorOffset());
    const bool matched = detail::repetition_parse_zero_min_recovery(
        ctx, element_ops(), std::numeric_limits<std::size_t>::max());
    PEGIUM_RECOVERY_TRACE(
        matched ? "[repeat * rule] stop offset="
                : "[repeat * rule] first element failed offset=",
        ctx.cursorOffset());
    return matched;
  } else if constexpr (is_plus) {
    const auto ops = element_ops();
    if (!detail::repetition_try_recovery_iteration(
            ctx, ops, /*skipBetweenIterations=*/false)) {
      PEGIUM_RECOVERY_TRACE("[repeat + rule] first element failed offset=",
                            ctx.cursorOffset());
      return false;
    }
    PEGIUM_RECOVERY_TRACE("[repeat + rule] first element ok offset=",
                          ctx.cursorOffset());
    while (true) {
      if (!detail::repetition_try_recovery_iteration(
              ctx, ops, /*skipBetweenIterations=*/true)) {
        if (ctx.frontierBlocked()) {
          if (detail::
                  repetition_blocked_frontier_stops_cleanly_after_window_progress(
                      ctx)) {
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
      return detail::repetition_parse_zero_min_recovery(ctx, element_ops(),
                                                        max);
    }
    const auto ops = element_ops();
    for (; repetitionCount < max; ++repetitionCount) {
      if (!detail::repetition_try_recovery_iteration(ctx, ops,
                                                     repetitionCount > 0)) {
        if (ctx.frontierBlocked()) {
          if (detail::
                  repetition_blocked_frontier_stops_cleanly_after_window_progress(
                      ctx)) {
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

template <std::size_t min, std::size_t max, NonNullableExpression Element>
bool Repetition<min, max, Element>::probeRecoverable(
    RecoveryContext &ctx) const {
  return attempt_fast_probe(ctx, _element) ||
         probe_locally_recoverable(_element, ctx);
}

template <std::size_t min, std::size_t max, NonNullableExpression Element>
bool Repetition<min, max, Element>::probeRecoverableAtEntry(
    RecoveryContext &ctx) const {
  if (attempt_fast_probe(ctx, _element)) {
    return true;
  }
  return probe_recoverable_at_entry(_element, ctx);
}

template <std::size_t min, std::size_t max, NonNullableExpression Element>
bool Repetition<min, max, Element>::probeRecoverableAtEntryConsumesVisible(
    RecoveryContext &ctx) const {
  if (ctx.probeFollowAcceptsHere()) {
    return false;
  }
  if (attempt_fast_probe(ctx, _element)) {
    return true;
  }
  if (probe_recoverable_at_entry_consumes_visible(_element, ctx)) {
    return true;
  }
  return detail::repetition_probe_resync_entry_consumes_visible(ctx,
                                                                 element_ops());
}

template <std::size_t min, std::size_t max, NonNullableExpression Element>
detail::RepetitionElementOps
Repetition<min, max, Element>::element_ops() const noexcept {
  return detail::RepetitionElementOps{
      .self = this,
      .shape = &shapeFacts(),
      .isOptional = is_optional,
      .minIsZero = min == 0,
      .parseNoEdits =
          +[](const void *self, RecoveryContext &ctx) {
            return attempt_parse_no_edits(
                ctx, static_cast<const Repetition *>(self)->_element);
          },
      .parseEditable =
          +[](const void *self, RecoveryContext &ctx) {
            return parse(static_cast<const Repetition *>(self)->_element,
                         ctx);
          },
      .fastProbe =
          +[](const void *self, RecoveryContext &ctx) {
            return attempt_fast_probe(
                ctx, static_cast<const Repetition *>(self)->_element);
          },
      .probeLocallyRecoverable =
          +[](const void *self, RecoveryContext &ctx) {
            return probe_locally_recoverable(
                static_cast<const Repetition *>(self)->_element, ctx);
          },
      .probeAtEntry =
          +[](const void *self, RecoveryContext &ctx) {
            return probe_recoverable_at_entry(
                static_cast<const Repetition *>(self)->_element, ctx);
          },
      .probeAtEntryConsumesVisible =
          +[](const void *self, RecoveryContext &ctx) {
            return probe_recoverable_at_entry_consumes_visible(
                static_cast<const Repetition *>(self)->_element, ctx);
          },
      .probeStartedWithoutEdits =
          +[](const void *self, RecoveryContext &ctx) {
            return probe_started_without_edits(
                ctx, static_cast<const Repetition *>(self)->_element);
          },
      .withElementFollow =
          +[](const void *self, RecoveryContext &ctx,
              pegium::utils::function_ref<bool()> body) {
            return static_cast<const Repetition *>(self)
                ->with_iteration_element_follow(ctx, body);
          },
  };
}

} // namespace pegium::parser
