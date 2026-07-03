#pragma once

/// Out-of-class definitions of `InfixRule`'s expect-mode members declared in
/// InfixRule.hpp: `Model::expect` and the nested `ParseEngine`'s `expect` and
/// `parse_tail_expect`. The operator matching itself (merging an operator's
/// blocked frontier, replaying the winning operator) already lives
/// externally in InfixRuleSupport.hpp's `InfixRuleExpectSupport`, so
/// `parse_tail_expect` below is mostly a thin loop calling into that support
/// struct. A translation unit that drives an InfixRule through expect mode
/// (completion-frontier construction) must include this header in addition
/// to InfixRule.hpp.

#include <pegium/core/parser/InfixRule.hpp>

namespace pegium::parser {

template <typename T, auto Left, auto Op, auto Right>
  requires DefaultConstructibleAstNode<T>
template <NonNullableExpression Element, InfixOperatorExpression... Operators>
bool InfixRule<T, Left, Op, Right>::Model<Element, Operators...>::expect(
    const void *self, ExpectContext &ctx) {
  return ParseEngine::expect(static_cast<const Model *>(self), ctx);
}

template <typename T, auto Left, auto Op, auto Right>
  requires DefaultConstructibleAstNode<T>
template <NonNullableExpression Element, InfixOperatorExpression... Operators>
bool InfixRule<T, Left, Op, Right>::Model<Element, Operators...>::ParseEngine::
    expect(const Model *model, ExpectContext &ctx) {
  const auto expressionStart = ctx.cursor();
  if (!parse(model->primary, ctx)) {
    return false;
  }
  if (ctx.frontierBlocked()) {
    return true;
  }
  ctx.skip();
  return parse_tail_expect(model, ctx, /*minPrecedence=*/0, expressionStart);
}

template <typename T, auto Left, auto Op, auto Right>
  requires DefaultConstructibleAstNode<T>
template <NonNullableExpression Element, InfixOperatorExpression... Operators>
bool InfixRule<T, Left, Op, Right>::Model<Element, Operators...>::ParseEngine::
    parse_tail_expect(const Model *model, ExpectContext &ctx,
                      std::int32_t minPrecedence, const char *lhsStart) {
  if (minPrecedence > kMaxPrecedence) {
    return true;
  }
  while (true) {
    if (ExpectSupport::try_merge_operator_frontier(model, ctx,
                                                   minPrecedence)) {
      ctx.clearFrontierBlock();
      return true;
    }

    const auto nodeCheckpoint = ctx.mark();
    std::int32_t nextMinPrecedence = 0;
    if (!ExpectSupport::try_match_operator(model, ctx, minPrecedence,
                                           nextMinPrecedence)) {
      ctx.rewind(nodeCheckpoint);
      return true;
    }
    ctx.skip();
    const auto rhsStart = ctx.cursor();
    auto rhsNoEditGuard = ctx.withEditTrackingDisabled();
    (void)rhsNoEditGuard;
    if (!parse(model->primary, ctx)) {
      ctx.rewind(nodeCheckpoint);
      return true;
    }
    if (ctx.frontierBlocked()) {
      return true;
    }
    ctx.skip();
    if (!parse_tail_expect(model, ctx, nextMinPrecedence, rhsStart)) {
      return false;
    }
    if (ctx.frontierBlocked()) {
      return true;
    }
    assert(model->_owner && "The owner must be set");
    ctx.exit(lhsStart, model->_owner);
  }
}

} // namespace pegium::parser
