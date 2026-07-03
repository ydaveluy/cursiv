#pragma once

/// Out-of-class definitions of `OrderedChoice`'s expect-mode members declared
/// in OrderedChoice.hpp: `parse_expect_impl`, `dispatch_choice_by_index`,
/// `collect_expect_results`, `collect_expect_result`, and
/// `replay_expect_branch_by_index`. A translation unit that drives an
/// OrderedChoice through expect mode (completion-frontier construction) must
/// include this header in addition to OrderedChoice.hpp.

#include <pegium/core/parser/OrderedChoice.hpp>

namespace pegium::parser {

template <Expression... Elements>
bool OrderedChoice<Elements...>::parse_expect_impl(ExpectContext &ctx) const {
  const auto base = ctx.mark();
  std::array<BranchResult, sizeof...(Elements)> branches{};
  collect_expect_results(ctx, base, branches,
                         std::make_index_sequence<sizeof...(Elements)>{});

  std::optional<std::size_t> bestIndex;
  for (std::size_t index = 0; index < branches.size(); ++index) {
    const auto &candidate = branches[index];
    if (!candidate.matched) {
      continue;
    }
    if (!bestIndex.has_value()) {
      bestIndex = index;
      continue;
    }
    const auto &best = branches[*bestIndex];
    if (candidate.cursor > best.cursor ||
        (candidate.cursor == best.cursor &&
         candidate.blocked != best.blocked && !candidate.blocked) ||
        (candidate.cursor == best.cursor &&
         candidate.blocked == best.blocked &&
         candidate.editCost < best.editCost)) {
      bestIndex = index;
    }
  }

  if (!bestIndex.has_value()) {
    ctx.rewind(base);
    return false;
  }

  ctx.rewind(base);
  if (!replay_expect_branch_by_index(ctx, *bestIndex)) {
    return false;
  }

  const auto &best = branches[*bestIndex];
  for (std::size_t index = 0; index < branches.size(); ++index) {
    if (index == *bestIndex) {
      continue;
    }
    const auto &candidate = branches[index];
    if (!candidate.matched || candidate.cursor != best.cursor ||
        candidate.editCost != best.editCost) {
      continue;
    }
    ctx.mergeFrontier(candidate.frontier);
  }
  if (!best.blocked) {
    ctx.clearFrontierBlock();
  }
  return true;
}

template <Expression... Elements>
template <typename Action, std::size_t I>
bool OrderedChoice<Elements...>::dispatch_choice_by_index(
    std::size_t bestIndex, Action &&action) const {
  if constexpr (I == sizeof...(Elements)) {
    return false;
  } else {
    if (bestIndex == I) {
      return action(std::get<I>(choices));
    }
    return dispatch_choice_by_index<Action, I + 1>(
        bestIndex, std::forward<Action>(action));
  }
}

template <Expression... Elements>
template <std::size_t... Is>
void OrderedChoice<Elements...>::collect_expect_results(
    ExpectContext &ctx, const ExpectContext::Checkpoint &base,
    std::array<BranchResult, sizeof...(Elements)> &branches,
    std::index_sequence<Is...>) const {
  (collect_expect_result<Is>(ctx, base, branches[Is]), ...);
}

template <Expression... Elements>
template <std::size_t I>
void OrderedChoice<Elements...>::collect_expect_result(
    ExpectContext &ctx, const ExpectContext::Checkpoint &base,
    BranchResult &result) const {
  collect_expect_branch(ctx, base, std::get<I>(choices), result);
}

template <Expression... Elements>
bool OrderedChoice<Elements...>::replay_expect_branch_by_index(
    ExpectContext &ctx, std::size_t bestIndex) const {
  return dispatch_choice_by_index(bestIndex, [&](const auto &c) {
    return parser::replay_expect_branch(ctx, c);
  });
}

} // namespace pegium::parser
