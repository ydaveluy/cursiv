#pragma once

/// Out-of-class definitions of `Literal`'s recovery- and expect-mode members
/// declared in Literal.hpp: `terminal_recovery_profile`,
/// `findReplaceCandidates`, `parse_terminal_recovery_impl`, the recovery
/// overload of `probeMatchHere`, and `probeRecoverable` /
/// `probeRecoverableAtEntry` / `probeRecoverableAtEntryConsumesVisible`. One
/// file rather than a recovery/expect pair: `parse_terminal_recovery_impl` is
/// a single template that dispatches between recovery mode
/// (`RecoveryParseModeContext`) and expect mode (the `else` arm, reached for
/// `ExpectContext`) with `if constexpr`, so the two modes do not resolve to
/// separate members the way they do for `OrderedChoice`. A translation unit
/// that drives a Literal through recovery or expect mode must include this
/// header in addition to Literal.hpp.

#include <pegium/core/parser/Literal.hpp>

namespace pegium::parser {

template <auto literal, bool case_sensitive>
detail::TerminalRecoveryProfile
Literal<literal, case_sensitive>::terminal_recovery_profile() const noexcept {
  return detail::TerminalRecoveryProfile{
      .self = this,
      .element = this,
      .match =
          +[](const void *self, const char *begin) noexcept {
            return static_cast<const Literal *>(self)->terminal(begin);
          },
      .shape = terminalShape,
      .literalValue = literalValue,
      .traceLabel = literalValue,
      .caseSensitive = case_sensitive,
      .hasLiteralText = true,
      .isKeyword = true,
      .splitInsertEligible = true,
      .insertUsesRuleVisibilityGate = false,
      .deleteScanLeafBeginsAtCursorStart = true,
      .wordLike = is_word_like_literal,
  };
}

template <auto literal, bool case_sensitive>
template <EditableParseModeContext Context>
detail::LiteralFuzzyCandidates
Literal<literal, case_sensitive>::findReplaceCandidates(
    Context &ctx, const char *cursorStart,
    detail::TerminalRecoveryFacts terminalRecoveryFacts) const noexcept {
  detail::LiteralFuzzyCandidates filteredCandidates;
  if constexpr (!allow_replace) {
    (void)ctx;
    (void)cursorStart;
    return filteredCandidates;
  } else {
    return detail::collect_literal_replace_candidates(
        ctx, cursorStart, getValue(), case_sensitive, terminalShape,
        terminalRecoveryFacts);
  }
}

template <auto literal, bool case_sensitive>
template <EditableParseModeContext Context>
bool Literal<literal, case_sensitive>::parse_terminal_recovery_impl(
    Context &ctx,
    const detail::TerminalRecoveryFacts &terminalRecoveryFacts) const {
  const auto effectiveRecoveryFacts =
      detail::effective_terminal_recovery_facts(ctx, terminalRecoveryFacts);
  const char *const cursorStart = ctx.cursor();
  const char *const matchedEnd = terminal(cursorStart);

  if constexpr (RecoveryParseModeContext<Context>) {
    if (matchedEnd != nullptr && !has_word_boundary_violation(ctx, matchedEnd)) {
      PEGIUM_RECOVERY_TRACE("[literal rule] direct match '", getValue(),
                            "' at ", ctx.cursorOffset());
      ctx.leaf(matchedEnd, this);
      return true;
    }
    return detail::recover_terminal_after_strict_miss(
        ctx, terminal_recovery_profile(), cursorStart, matchedEnd,
        effectiveRecoveryFacts);
  } else {
    if (ctx.reachedAnchor()) {
      ctx.addKeyword(this);
      return true;
    }

    if (matchedEnd != nullptr && ctx.canTraverseUntil(matchedEnd) &&
        !has_word_boundary_violation(ctx, matchedEnd)) {
      ctx.leaf(matchedEnd, this);
      return true;
    }
    if (const auto anchorDistance =
            static_cast<std::size_t>(ctx.anchor - cursorStart);
        cursorStart < ctx.anchor && anchorDistance <= literal.size()) {
      bool prefixMatches = true;
      for (std::size_t index = 0; index < anchorDistance; ++index) {
        const auto lhs = static_cast<unsigned char>(cursorStart[index]);
        const auto rhs = static_cast<unsigned char>(literal[index]);
        if constexpr (case_sensitive) {
          if (lhs != rhs) {
            prefixMatches = false;
            break;
          }
        } else if (utils::tolower(lhs) != rhs) {
          prefixMatches = false;
          break;
        }
      }
      if (prefixMatches) {
        ctx.addKeyword(this);
        return true;
      }
    }

    if (effectiveRecoveryFacts.localRecoveryBlocked) {
      return false;
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

template <auto literal, bool case_sensitive>
bool Literal<literal, case_sensitive>::probeMatchHere(
    RecoveryContext &ctx) const noexcept {
  const char *const cursorStart = ctx.cursor();
  if (const char *const matchedEnd = terminal(cursorStart);
      matchedEnd != nullptr && !has_word_boundary_violation(ctx, matchedEnd)) {
    return true;
  }
  if (ctx.hasHadEdits() &&
      !detail::allows_fuzzy_replace_after_prior_edits(terminalShape)) {
    return false;
  }
  if constexpr (allow_replace) {
    const auto fuzzyCandidates = findReplaceCandidates(ctx, cursorStart);
    if (!fuzzyCandidates.empty()) {
      return true;
    }
  }
  return false;
}

template <auto literal, bool case_sensitive>
bool Literal<literal, case_sensitive>::probeRecoverable(
    RecoveryContext &ctx) const noexcept {
  return detail::literal_probe_recoverable(ctx, terminal_recovery_profile());
}

template <auto literal, bool case_sensitive>
bool Literal<literal, case_sensitive>::probeRecoverableAtEntry(
    RecoveryContext &ctx) const noexcept {
  return detail::literal_probe_recoverable_at_entry(
      ctx, terminal_recovery_profile());
}

template <auto literal, bool case_sensitive>
bool Literal<literal, case_sensitive>::probeRecoverableAtEntryConsumesVisible(
    RecoveryContext &ctx) const noexcept {
  return detail::literal_probe_recoverable_at_entry_consumes_visible(
      ctx, terminal_recovery_profile());
}

} // namespace pegium::parser
