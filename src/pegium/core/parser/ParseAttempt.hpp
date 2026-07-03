#pragma once

/// Helpers that parse or probe expressions while preserving caller state on failure.

#include <algorithm>
#include <concepts>
#include <type_traits>

#include <pegium/core/parser/RecoveryContext.hpp>
#include <pegium/core/parser/ParseExpression.hpp>
#include <pegium/core/parser/ParseMode.hpp>

namespace pegium::parser {

template <typename E, typename = void>
struct IsFailureSafeExpression : std::false_type {};

template <typename E>
struct IsFailureSafeExpression<
    E, std::void_t<decltype(std::remove_cvref_t<E>::isFailureSafe)>>
    : std::bool_constant<std::remove_cvref_t<E>::isFailureSafe> {};

template <Expression E>
inline constexpr bool requires_checkpoint_on_failure_v =
    !IsFailureSafeExpression<E>::value;

namespace detail {

template <typename E>
concept LocalRecoveryProbeCapableExpression =
    Expression<E> &&
    requires(const std::remove_cvref_t<E> &expression, RecoveryContext &ctx) {
      { expression.probeRecoverable(ctx) } -> std::same_as<bool>;
    };

struct FastProbeAccess {
  template <typename Expr, typename Context>
  static bool probe(const Expr &expression, Context &ctx)
    requires requires {
      { expression.fast_probe_impl(ctx) } -> std::same_as<bool>;
    }
  {
    return expression.fast_probe_impl(ctx);
  }
};

template <typename E, typename Context>
concept FastProbeCapableExpression =
    Expression<E> &&
    requires(const std::remove_cvref_t<E> &expression, Context &ctx) {
      { FastProbeAccess::probe(expression, ctx) } -> std::same_as<bool>;
    };

template <Expression E>
inline bool attempt_parse_recovery_strict_view(RecoveryContext &ctx,
                                               const E &expression) {
  if constexpr (!requires_checkpoint_on_failure_v<E>) {
    TrackedParseContext &strictCtx = ctx;
    return parse(expression, strictCtx);
  } else {
    const auto checkpoint = ctx.mark();
    if (TrackedParseContext &strictCtx = ctx; parse(expression, strictCtx)) {
      return true;
    }
    ctx.rewind(checkpoint);
    return false;
  }
}

} // namespace detail

template <Expression E, StrictParseModeContext Context>
inline bool attempt_parse_strict(Context &ctx, const E &expression) {
  if constexpr (!requires_checkpoint_on_failure_v<E>) {
    return parse(expression, ctx);
  } else {
    const auto checkpoint = ctx.mark();
    if (parse(expression, ctx)) {
      return true;
    }
    ctx.rewind(checkpoint);
    return false;
  }
}

template <Expression E>
inline bool attempt_parse_no_edits(RecoveryContext &ctx, const E &expression) {
  auto noEditGuard = ctx.withEditTrackingDisabled();
  (void)noEditGuard;
  return detail::attempt_parse_recovery_strict_view(ctx, expression);
}

template <Expression E>
inline bool probe_started_without_edits(RecoveryContext &ctx,
                                        const E &expression);

struct NoEditParseObservation {
  bool matched = false;
  bool startedWithoutEdits = false;
};

template <Expression E>
inline NoEditParseObservation observe_no_edit_parse(RecoveryContext &ctx,
                                                    const E &expression) {
  const auto startOffset = ctx.cursorOffset();
  const auto furthestExploredOffsetBefore = ctx.maxCursorOffset();
  auto noEditGuard = ctx.withEditTrackingDisabled();
  (void)noEditGuard;
  TrackedParseContext &strictCtx = ctx;
  const auto cursor_advanced = [&] {
    return ctx.cursorOffset() > startOffset ||
           ctx.maxCursorOffset() >
               std::max(startOffset, furthestExploredOffsetBefore);
  };
  const auto failed_observation_with = [&](bool cursorAdvanced) {
    bool startedWithoutEdits = cursorAdvanced;
    if (!startedWithoutEdits) {
      startedWithoutEdits = probe_started_without_edits(ctx, expression);
    }
    return NoEditParseObservation{
        .matched = false,
        .startedWithoutEdits = startedWithoutEdits,
    };
  };
  bool matched = false;
  if constexpr (!requires_checkpoint_on_failure_v<E>) {
    matched = parse(expression, strictCtx);
  } else {
    const auto checkpoint = ctx.mark();
    matched = parse(expression, strictCtx);
    if (!matched) {
      const bool cursorAdvanced = cursor_advanced();
      ctx.rewind(checkpoint);
      return failed_observation_with(cursorAdvanced);
    }
  }
  if (!matched) {
    return failed_observation_with(cursor_advanced());
  }
  return {
      .matched = true,
      .startedWithoutEdits = true,
  };
}

template <Expression E>
inline bool attempt_parse_editable(RecoveryContext &ctx, const E &expression) {
  if (!ctx.isInRecoveryPhase() && !ctx.hasPendingCommittedRecoveryEdits() &&
      !ctx.allowsCompletedWindowContinuationRecovery()) {
    return detail::attempt_parse_recovery_strict_view(ctx, expression);
  }
  if constexpr (!requires_checkpoint_on_failure_v<E>) {
    return parse(expression, ctx);
  } else {
    const auto checkpoint = ctx.mark();
    if (parse(expression, ctx)) {
      return true;
    }
    ctx.rewind(checkpoint);
    return false;
  }
}

template <Expression E, StrictParseModeContext Context>
inline bool attempt_fast_probe(Context &ctx, const E &expression) {
  if constexpr (detail::FastProbeCapableExpression<E, Context>) {
    return detail::FastProbeAccess::probe(expression, ctx);
  } else {
    return probe(expression, ctx);
  }
}

template <Expression E>
inline bool attempt_fast_probe(RecoveryContext &ctx, const E &expression) {
  if constexpr (detail::FastProbeCapableExpression<E, RecoveryContext>) {
    return detail::FastProbeAccess::probe(expression, ctx);
  } else if constexpr (detail::FastProbeCapableExpression<E,
                                                          TrackedParseContext>) {
    TrackedParseContext &strictCtx = ctx;
    return detail::FastProbeAccess::probe(expression, strictCtx);
  } else {
    TrackedParseContext &strictCtx = ctx;
    return probe(expression, strictCtx);
  }
}

template <Expression E>
inline bool attempt_fast_probe(ExpectContext &ctx, const E &expression) {
  return parse(expression, ctx);
}

template <Expression E>
inline bool probe_locally_recoverable(const E &expression, RecoveryContext &ctx) {
  if constexpr (detail::LocalRecoveryProbeCapableExpression<E>) {
    return expression.probeRecoverable(ctx);
  } else {
    (void)expression;
    (void)ctx;
    return false;
  }
}

/// Strict-or-fuzzy "match at the cursor" probe used by `NotPredicate`.
/// Elements implementing `probeMatchHere` (Literal, OrderedChoice) report a
/// low-cost fuzzy candidate as a match without scanning past the cursor;
/// everything else falls back to the strict probe so the negative-lookahead
/// semantics stay tight. Available in both strict (TrackedParseContext) and
/// recovery (RecoveryContext) modes so a `many(!K + Item)` doesn't gobble a
/// truncated/typoed keyword in either pass.
template <Expression E, typename Context>
inline bool probe_match_here(const E &expression, Context &ctx) {
  if constexpr (requires {
                  { expression.probeMatchHere(ctx) } -> std::same_as<bool>;
                }) {
    return expression.probeMatchHere(ctx);
  } else if constexpr (StrictParseModeContext<Context>) {
    return parser::probe(expression, ctx);
  } else {
    TrackedParseContext &strictCtx = ctx;
    return parser::probe(expression, strictCtx);
  }
}

template <Expression E>
inline bool probe_recoverable_at_entry(const E &expression,
                                       RecoveryContext &ctx) {
  if constexpr (requires {
                  { expression.probeRecoverableAtEntry(ctx) } ->
                      std::same_as<bool>;
                }) {
    return expression.probeRecoverableAtEntry(ctx);
  } else {
    return false;
  }
}

template <Expression E>
inline bool probe_recoverable_at_entry_consumes_visible(
    const E &expression, RecoveryContext &ctx) {
  if constexpr (requires {
                  { expression.probeRecoverableAtEntryConsumesVisible(ctx) } ->
                      std::same_as<bool>;
                }) {
    return expression.probeRecoverableAtEntryConsumesVisible(ctx);
  } else {
    (void)expression;
    (void)ctx;
    return false;
  }
}

template <Expression E>
inline bool probe_started_without_edits(RecoveryContext &ctx,
                                        const E &expression) {
  const auto startOffset = ctx.cursorOffset();
  const auto checkpoint = ctx.mark();
  const char *const savedFurthestExploredCursor =
      ctx.maxCursor();
  ctx.restoreMaxCursor(ctx.cursor());
  if (attempt_fast_probe(ctx, expression)) {
    ctx.rewind(checkpoint);
    ctx.restoreMaxCursor(savedFurthestExploredCursor);
    return true;
  }
  (void)attempt_parse_no_edits(ctx, expression);
  const bool started =
      ctx.cursorOffset() > startOffset ||
      ctx.maxCursorOffset() > startOffset;
  ctx.rewind(checkpoint);
  ctx.restoreMaxCursor(savedFurthestExploredCursor);
  return started;
}

} // namespace pegium::parser
