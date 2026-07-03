#pragma once

/// Out-of-class definitions of `InfixRule`'s recovery-mode members declared
/// in InfixRule.hpp: `InfixRule::probeRecoverable`, `Model::recover`,
/// `Model::probeRecoverable`, and the nested `ParseEngine`'s `recover`,
/// `tail_ops`, and `try_match_operator_editable`. A translation unit
/// that drives an InfixRule through recovery mode must include this header
/// in addition to InfixRule.hpp.

#include <pegium/core/parser/InfixRule.hpp>

namespace pegium::parser {

template <typename T, auto Left, auto Op, auto Right>
  requires DefaultConstructibleAstNode<T>
bool InfixRule<T, Left, Op, Right>::probeRecoverable(
    RecoveryContext &ctx) const {
  // `.probeRecoverable` is null whenever recovery is compiled out
  // (PEGIUM_DISABLE_RECOVERY). Unlike most other recovery-mode
  // accessors this one is reachable directly (a raw `InfixRule` satisfies
  // `RecoveryProbeCapableExpression`, so `probe_locally_recoverable`/
  // callers of that concept can call it on an `InfixRule` reference with
  // no intervening `Wrapper`), so this method has to know about the
  // compiled-out case itself. Branching on the compile-time flag rather
  // than the (always non-null when it is true) function pointer keeps
  // this identical to a plain unconditional call in a recovery-capable
  // build.
  if constexpr (detail::kRecoveryCompiledIn) {
    assert(_obj && _ops.probeRecoverable && "Missing recovery probe wrapper!");
    return _ops.probeRecoverable(_obj, ctx);
  } else {
    return false;
  }
}

template <typename T, auto Left, auto Op, auto Right>
  requires DefaultConstructibleAstNode<T>
template <NonNullableExpression Element, InfixOperatorExpression... Operators>
bool InfixRule<T, Left, Op, Right>::Model<Element, Operators...>::recover(
    const void *self, RecoveryContext &ctx) {
  return ParseEngine::recover(static_cast<const Model *>(self), ctx);
}

template <typename T, auto Left, auto Op, auto Right>
  requires DefaultConstructibleAstNode<T>
template <NonNullableExpression Element, InfixOperatorExpression... Operators>
bool InfixRule<T, Left, Op, Right>::Model<Element, Operators...>::
    probeRecoverable(const void *self, RecoveryContext &ctx) {
  const auto *model = static_cast<const Model *>(self);
  detail::ProbeRestoreScope guard{ctx};
  const auto startOffset = ctx.cursorOffset();
  ctx.restoreMaxCursor(ctx.cursor());
  const bool matchedPrimary =
      parser::attempt_parse_no_edits(ctx, model->primary);
  return matchedPrimary || ctx.maxCursorOffset() > startOffset;
}

template <typename T, auto Left, auto Op, auto Right>
  requires DefaultConstructibleAstNode<T>
template <NonNullableExpression Element, InfixOperatorExpression... Operators>
bool InfixRule<T, Left, Op, Right>::Model<Element, Operators...>::ParseEngine::
    recover(const Model *model, RecoveryContext &ctx) {
  const auto expressionStart = ctx.cursor();
  if (!parse(model->primary, ctx)) {
    return false;
  }
  ctx.skip();
  const auto replayResult = detail::infix_parse_tail_editable(
      ctx, tail_ops(model), /*minPrecedence=*/0, expressionStart);
  return replayResult.matched;
}

template <typename T, auto Left, auto Op, auto Right>
  requires DefaultConstructibleAstNode<T>
template <NonNullableExpression Element, InfixOperatorExpression... Operators>
detail::InfixTailOps
InfixRule<T, Left, Op, Right>::Model<Element, Operators...>::ParseEngine::
    tail_ops(const Model *model) noexcept {
  return detail::InfixTailOps{
      .model = model,
      .owner = model->_owner,
      .maxPrecedence = kMaxPrecedence,
      .operatorFastProbe =
          +[](const void *self, RecoveryContext &ctx,
              std::int32_t minPrecedence) {
            return has_operator_fast_probe(
                static_cast<const Model *>(self), ctx, minPrecedence);
          },
      .matchOperatorEditable =
          +[](const void *self, RecoveryContext &ctx,
              std::int32_t minPrecedence, std::int32_t &nextMinPrecedence) {
            return try_match_operator_editable(
                static_cast<const Model *>(self), ctx, minPrecedence,
                nextMinPrecedence);
          },
      .parsePrimary =
          +[](const void *self, RecoveryContext &ctx) {
            return parse(static_cast<const Model *>(self)->primary, ctx);
          },
      .parsePrimaryNoEdits =
          +[](const void *self, RecoveryContext &ctx) {
            return parser::attempt_parse_no_edits(
                ctx, static_cast<const Model *>(self)->primary);
          },
      .fastProbePrimary =
          +[](const void *self, RecoveryContext &ctx) {
            return parser::attempt_fast_probe(
                ctx, static_cast<const Model *>(self)->primary);
          },
      .probeLocallyRecoverablePrimary =
          +[](const void *self, RecoveryContext &ctx) {
            return parser::probe_locally_recoverable(
                static_cast<const Model *>(self)->primary, ctx);
          },
  };
}

template <typename T, auto Left, auto Op, auto Right>
  requires DefaultConstructibleAstNode<T>
template <NonNullableExpression Element, InfixOperatorExpression... Operators>
template <std::size_t I>
bool InfixRule<T, Left, Op, Right>::Model<Element, Operators...>::ParseEngine::
    try_match_operator_editable(const Model *model, RecoveryContext &ctx,
                                std::int32_t minPrecedence,
                                std::int32_t &nextMinPrecedence) {
  const auto &op = std::get<I>(model->ops);
  constexpr auto precedence =
      static_cast<std::int32_t>(sizeof...(Operators) - I);
  if (precedence < minPrecedence) {
    return false;
  }
  if (!parser::attempt_fast_probe(ctx, op)) {
    if constexpr (I + 1 == sizeof...(Operators)) {
      return false;
    } else {
      return try_match_operator_editable<I + 1>(model, ctx, minPrecedence,
                                                nextMinPrecedence);
    }
  }
  // RAII restore of the edit permissions: an exception inside
  // `attempt_parse_editable` (e.g. cancellation) must not leave the
  // context with both flags pinned to false.
  auto editGuard = ctx.withEditPermissions(false, false);
  (void)editGuard;
  const auto operatorCheckpoint = ctx.mark();
  if (parser::attempt_parse_editable(ctx, op)) {
    if (!detail::infix_operator_shadowed<std::remove_cvref_t<decltype(op)>,
                                         Operators...>(ctx.cursor(),
                                                       ctx.end)) {
      nextMinPrecedence =
          detail::infix_next_min_precedence<decltype(op)>(precedence);
      return true;
    }
    ctx.rewind(operatorCheckpoint);
  }
  if constexpr (I + 1 == sizeof...(Operators)) {
    return false;
  } else {
    return try_match_operator_editable<I + 1>(model, ctx, minPrecedence,
                                              nextMinPrecedence);
  }
}

} // namespace pegium::parser
