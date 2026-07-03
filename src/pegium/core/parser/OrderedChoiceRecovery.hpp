#pragma once

/// Out-of-class definitions of `OrderedChoice`'s recovery-mode members
/// declared in OrderedChoice.hpp: `probeRecoverable`, `probeRecoverableAtEntry`,
/// `probeRecoverableAtEntryConsumesVisible`, `parse_recovery_impl`,
/// `branch_ops`, and the recovery overload of `match_choice`. A
/// translation unit that drives an OrderedChoice through recovery mode must
/// include this header in addition to OrderedChoice.hpp.

#include <pegium/core/parser/OrderedChoice.hpp>

namespace pegium::parser {

template <Expression... Elements>
bool OrderedChoice<Elements...>::probeRecoverable(RecoveryContext &ctx) const {
  return any_choice([&](const auto &c) {
    return parser::attempt_fast_probe(ctx, c) ||
           probe_locally_recoverable(c, ctx);
  });
}

template <Expression... Elements>
bool OrderedChoice<Elements...>::probeRecoverableAtEntry(
    RecoveryContext &ctx) const {
  return any_choice(
      [&](const auto &c) { return probe_recoverable_at_entry(c, ctx); });
}

template <Expression... Elements>
bool OrderedChoice<Elements...>::probeRecoverableAtEntryConsumesVisible(
    RecoveryContext &ctx) const {
  return any_choice([&](const auto &c) {
    return parser::attempt_fast_probe(ctx, c) ||
           probe_recoverable_at_entry_consumes_visible(c, ctx);
  });
}

template <Expression... Elements>
bool OrderedChoice<Elements...>::parse_recovery_impl(
    RecoveryContext &ctx) const {
  if (ctx.recoveryDescentInactive()) {
    TrackedParseContext &strictCtx = ctx;
    return parse_strict_impl(strictCtx);
  }
  if (!ctx.isInRecoveryPhase() && ctx.hasPendingRecoveryWindows() &&
      ctx.cursorOffset() < ctx.pendingRecoveryWindowBeginOffset()) {
    return match_choice(ctx);
  }
  const auto branchOps =
      branch_ops(std::make_index_sequence<sizeof...(Elements)>{});
  return detail::run_choice_recovery(ctx, this, branchOps);
}

template <Expression... Elements>
template <std::size_t... Is>
std::array<detail::ChoiceBranchOps, sizeof...(Elements)>
OrderedChoice<Elements...>::branch_ops(
    std::index_sequence<Is...>) const noexcept {
  return {detail::ChoiceBranchOps{
      .element = std::addressof(std::get<Is>(choices)),
      .parseNoEdits =
          +[](const void *element, RecoveryContext &ctx) {
            return attempt_parse_no_edits(
                ctx, *static_cast<const std::remove_cvref_t<
                         std::tuple_element_t<Is, std::tuple<Elements...>>> *>(
                         element));
          },
      .parseEditable =
          +[](const void *element, RecoveryContext &ctx) {
            return attempt_parse_editable(
                ctx, *static_cast<const std::remove_cvref_t<
                         std::tuple_element_t<Is, std::tuple<Elements...>>> *>(
                         element));
          }}...};
}

template <Expression... Elements>
template <RecoveryParseModeContext Context>
bool OrderedChoice<Elements...>::match_choice(Context &ctx) const {
  return any_choice([&](const auto &c) {
    const auto checkpoint = ctx.mark();
    if (parse(c, ctx)) {
      return true;
    }
    ctx.rewind(checkpoint);
    return false;
  });
}

} // namespace pegium::parser
