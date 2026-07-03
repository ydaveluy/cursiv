#pragma once

/// Out-of-class definitions of `Repetition`'s expect-mode members declared in
/// Repetition.hpp: `parse_expect_impl` and `try_expect_iteration`. A
/// translation unit that drives a Repetition through expect mode
/// (completion-frontier construction) must include this header in addition
/// to Repetition.hpp.

#include <pegium/core/parser/Repetition.hpp>

namespace pegium::parser {

template <std::size_t min, std::size_t max, NonNullableExpression Element>
bool Repetition<min, max, Element>::parse_expect_impl(
    ExpectContext &ctx) const {
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

template <std::size_t min, std::size_t max, NonNullableExpression Element>
typename Repetition<min, max, Element>::ExpectIterationResult
Repetition<min, max, Element>::try_expect_iteration(ExpectContext &ctx,
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

} // namespace pegium::parser
