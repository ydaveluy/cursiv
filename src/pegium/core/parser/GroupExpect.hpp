#pragma once

/// Out-of-class definition of `Group`'s expect-mode member declared in
/// Group.hpp: `parse_elements_expect`. A translation unit that drives a
/// Group through expect mode (completion-frontier construction) must
/// include this header in addition to Group.hpp.

#include <pegium/core/parser/Group.hpp>

namespace pegium::parser {

template <Expression... Elements>
template <ParseModeContext Context, std::size_t I>
bool Group<Elements...>::parse_elements_expect(Context &ctx) const {
  const auto checkpoint = ctx.mark();
  if (!parse(std::get<I>(elements), ctx)) {
    return false;
  }
  if (!ctx.frontierBlocked()) {
    return parse_elements<Context, I + 1>(ctx);
  }
  if constexpr (std::remove_cvref_t<
                    decltype(std::get<I>(elements))>::nullable) {
    auto currentFrontier = capture_frontier_since(ctx, checkpoint);
    ctx.clearFrontierBlock();
    if (parse_elements<Context, I + 1>(ctx)) {
      const bool tailBlocked = ctx.frontierBlocked();
      merge_captured_frontier(ctx, currentFrontier, !tailBlocked);
      return true;
    }
    ctx.rewind(checkpoint);
    merge_captured_frontier(ctx, currentFrontier, false);
  }
  return true;
}

} // namespace pegium::parser
