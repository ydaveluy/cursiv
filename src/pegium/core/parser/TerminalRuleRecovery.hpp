#pragma once

/// Out-of-class definitions of `TerminalRule`'s recovery-mode members
/// declared in TerminalRule.hpp: `terminal_recovery_profile`,
/// `parse_terminal_recovery_impl`, and `probeRecoverable` /
/// `probeRecoverableAtEntry` / `probeRecoverableAtEntryConsumesVisible`. One
/// file rather than a recovery/expect pair: `parse_terminal_recovery_impl` is
/// a single template that dispatches between recovery mode
/// (`RecoveryParseModeContext`) and expect mode (the `else` arm, reached for
/// `ExpectContext`) with `if constexpr`, so the two modes do not resolve to
/// separate members the way they do for `OrderedChoice`. A translation unit
/// that drives a TerminalRule through recovery or expect mode must include
/// this header in addition to TerminalRule.hpp.

#include <pegium/core/parser/TerminalRule.hpp>

namespace pegium::parser {

template <typename T>
  requires detail::SupportedRuleValueType<T>
bool TerminalRule<T>::probeRecoverable(RecoveryContext &ctx) const noexcept {
  return detail::terminal_rule_probe_recoverable(ctx,
                                                 terminal_recovery_profile());
}

template <typename T>
  requires detail::SupportedRuleValueType<T>
bool TerminalRule<T>::probeRecoverableAtEntry(RecoveryContext &ctx) const
    noexcept {
  return detail::terminal_rule_probe_recoverable_at_entry(
      ctx, terminal_recovery_profile());
}

template <typename T>
  requires detail::SupportedRuleValueType<T>
bool TerminalRule<T>::probeRecoverableAtEntryConsumesVisible(
    RecoveryContext &ctx) const noexcept {
  return detail::terminal_rule_probe_recoverable_at_entry_consumes_visible(
      ctx, terminal_recovery_profile());
}

template <typename T>
  requires detail::SupportedRuleValueType<T>
detail::TerminalRecoveryProfile
TerminalRule<T>::terminal_recovery_profile() const noexcept {
  return detail::TerminalRecoveryProfile{
      .self = this,
      .element = this,
      .match =
          +[](const void *self, const char *begin) noexcept {
            return static_cast<const TerminalRule *>(self)->terminal(begin);
          },
      .shape = _terminalShape,
      .literalValue = _literalRecoveryMetadata.has_value()
                          ? _literalRecoveryMetadata->value
                          : std::string_view{},
      .traceLabel = getName(),
      .caseSensitive = !_literalRecoveryMetadata.has_value() ||
                       _literalRecoveryMetadata->caseSensitive,
      .hasLiteralText = _literalRecoveryMetadata.has_value(),
      .isKeyword = false,
      .splitInsertEligible = false,
      .insertUsesRuleVisibilityGate = true,
      .deleteScanLeafBeginsAtCursorStart = false,
      .wordLike = false,
  };
}

template <typename T>
  requires detail::SupportedRuleValueType<T>
template <EditableParseModeContext Context>
bool TerminalRule<T>::parse_terminal_recovery_impl(
    Context &ctx,
    const detail::TerminalRecoveryFacts &terminalRecoveryFacts) const {
  const auto effectiveRecoveryFacts =
      detail::effective_terminal_recovery_facts(ctx, terminalRecoveryFacts);
  const char *const cursorStart = ctx.cursor();
  const char *const matchedEnd = terminal(cursorStart);

  if constexpr (RecoveryParseModeContext<Context>) {
    const bool notInPhase = !ctx.isInRecoveryPhase() &&
                            !ctx.hasPendingCommittedRecoveryEdits();
    if (notInPhase) {
      PEGIUM_RECOVERY_TRACE("[terminal rule] enter ", getName(),
                            " offset=", ctx.cursorOffset());
    }
    if (matchedEnd != nullptr) {
      PEGIUM_RECOVERY_TRACE("[terminal rule] direct match ", getName(),
                            " offset=", ctx.cursorOffset());
      ctx.leaf(matchedEnd, this);
      return true;
    }
    // Outside an active recovery phase a strict miss stops here (no local
    // recovery is attempted until a window opens).
    if (notInPhase) {
      PEGIUM_RECOVERY_TRACE("[terminal rule] strict fail ", getName(),
                            " offset=", ctx.cursorOffset());
      return false;
    }
    return detail::recover_terminal_after_strict_miss(
        ctx, terminal_recovery_profile(), cursorStart, matchedEnd,
        effectiveRecoveryFacts);
  } else {
    if (ctx.reachedAnchor()) {
      ctx.addRule(this);
      return true;
    }
    if (matchedEnd != nullptr && ctx.canTraverseUntil(matchedEnd)) {
      ctx.leaf(matchedEnd, this);
      return true;
    }
    if (!ctx.canEdit()) {
      return false;
    }
    const auto profile = terminal_recovery_profile();
    return detail::run_terminal_local_recovery(
        ctx, profile, cursorStart, matchedEnd,
        detail::collect_terminal_replace_candidates(
            ctx, profile, cursorStart, effectiveRecoveryFacts),
        effectiveRecoveryFacts);
  }
}

} // namespace pegium::parser
