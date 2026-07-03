#pragma once

/// Non-template parse bodies shared by the named rule shells `ParserRule`
/// and `DataTypeRule`.
///
/// The rule shells' parse machinery never reads the rule's value type: each
/// body touches only the parse context, the type-erased element `Wrapper`,
/// the rule's optional local skipper and the rule's grammar identity. Hosting
/// the bodies here keeps one copy per parse mode instead of one per rule
/// value type:
///  - the strict bodies live in this header and are forced inline into each
///    shell's `parse_impl` (they are the clean-parse hot path, and inlining
///    keeps the strict object code identical to the former member bodies);
///  - the recovery and expect bodies, and the recovery-probe runner, are
///    cold and compile once in RuleParseSupport.cpp.
///
/// What stays in the shell headers: the concept-mandated `probeRecoverable*`
/// method names (thin adapters onto `run_rule_recovery_probe`), the
/// `rule`/`recover`/`expect` virtual overrides, and everything that genuinely
/// depends on the value type (AST materialization, value conversion,
/// reflection init).

#include <cstdint>
#include <optional>
#include <utility>

#include <pegium/core/grammar/AbstractRule.hpp>
#include <pegium/core/parser/RecoveryContext.hpp>
#include <pegium/core/parser/ParseExpression.hpp>
#include <pegium/core/parser/ParseMode.hpp>
#include <pegium/core/parser/RecoveryTrace.hpp>
#include <pegium/core/parser/Skipper.hpp>
#include <pegium/core/utils/Cancellation.hpp>

namespace pegium::parser::detail {

// Runs `fn` with the rule's optional local skipper installed for its
// duration. The skipper guard is released as `fn` returns, before any
// surrounding `ctx.exit`.
template <typename Context, typename Fn>
[[gnu::always_inline]] inline decltype(auto)
run_under_local_skipper(const std::optional<Skipper> &localSkipper,
                        Context &ctx, Fn &&fn) {
  if (localSkipper.has_value()) {
    auto localSkipperGuard = ctx.with_skipper(*localSkipper);
    (void)localSkipperGuard;
    return std::forward<Fn>(fn)();
  }
  return std::forward<Fn>(fn)();
}

/// Strict `ParserRule` body, shared by the `ParseContext` and
/// `TrackedParseContext` overloads below. Templated over the context only so
/// the shadowed `with_skipper` (skip-cache invalidation) and the wrapper's
/// per-context parse slot dispatch statically; the body itself is
/// context-uniform.
template <StrictParseModeContext Context>
[[gnu::always_inline]] [[nodiscard]] inline bool
named_rule_strict_body(const Wrapper &wrapper,
                       const std::optional<Skipper> &localSkipper,
                       const grammar::AbstractRule *self, Context &ctx) {
  PEGIUM_RECOVERY_TRACE("[rule rule] enter ", self->getName(),
                        " offset=", ctx.cursorOffset());
  const auto nodeStartCheckpoint = ctx.enter();
  const bool matched = run_under_local_skipper(
      localSkipper, ctx, [&] { return parse(wrapper, ctx); });
  if (!matched) {
    PEGIUM_RECOVERY_TRACE("[rule rule] fail ", self->getName(),
                          " offset=", ctx.cursorOffset());
    return false;
  }
  ctx.exit(nodeStartCheckpoint, self);
  utils::throw_if_cancelled(ctx.cancellationToken());
  PEGIUM_RECOVERY_TRACE("[rule rule] ok ", self->getName(),
                        " offset=", ctx.cursorOffset());
  return true;
}

/// Strict `DataTypeRule` body; same context-only template rationale as
/// `named_rule_strict_body`. Deliberately leaner than the `ParserRule` body:
/// no trace points and no per-rule cancellation throttle.
template <StrictParseModeContext Context>
[[gnu::always_inline]] [[nodiscard]] inline bool
data_type_rule_strict_body(const Wrapper &wrapper,
                           const std::optional<Skipper> &localSkipper,
                           const grammar::AbstractRule *self, Context &ctx) {
  const auto nodeStartCheckpoint = ctx.enter();
  const bool matched = run_under_local_skipper(
      localSkipper, ctx, [&] { return parse(wrapper, ctx); });
  if (!matched) {
    return false;
  }
  ctx.exit(nodeStartCheckpoint, self);
  return true;
}

[[gnu::always_inline]] [[nodiscard]] inline bool
parse_named_rule_strict(const Wrapper &wrapper,
                        const std::optional<Skipper> &localSkipper,
                        const grammar::AbstractRule *self, ParseContext &ctx) {
  return named_rule_strict_body(wrapper, localSkipper, self, ctx);
}

[[gnu::always_inline]] [[nodiscard]] inline bool
parse_named_rule_strict(const Wrapper &wrapper,
                        const std::optional<Skipper> &localSkipper,
                        const grammar::AbstractRule *self,
                        TrackedParseContext &ctx) {
  return named_rule_strict_body(wrapper, localSkipper, self, ctx);
}

[[gnu::always_inline]] [[nodiscard]] inline bool
parse_data_type_rule_strict(const Wrapper &wrapper,
                            const std::optional<Skipper> &localSkipper,
                            const grammar::AbstractRule *self,
                            ParseContext &ctx) {
  return data_type_rule_strict_body(wrapper, localSkipper, self, ctx);
}

[[gnu::always_inline]] [[nodiscard]] inline bool
parse_data_type_rule_strict(const Wrapper &wrapper,
                            const std::optional<Skipper> &localSkipper,
                            const grammar::AbstractRule *self,
                            TrackedParseContext &ctx) {
  return data_type_rule_strict_body(wrapper, localSkipper, self, ctx);
}

/// Recovery-mode `ParserRule` body: the rule-entry / recursion-depth budget
/// caps, the recovery descent, and the partial top-level recovery commit
/// policy. Compiled once in RuleParseSupport.cpp.
[[nodiscard]] bool
parse_named_rule_recover(const Wrapper &wrapper,
                         const std::optional<Skipper> &localSkipper,
                         const grammar::AbstractRule *self,
                         RecoveryContext &ctx);

/// Expect-mode `ParserRule` body (completion-frontier construction).
/// Compiled once in RuleParseSupport.cpp.
[[nodiscard]] bool
parse_named_rule_expect(const Wrapper &wrapper,
                        const std::optional<Skipper> &localSkipper,
                        const grammar::AbstractRule *self, ExpectContext &ctx);

/// Recovery-mode `DataTypeRule` body. Compiled once in RuleParseSupport.cpp.
[[nodiscard]] bool
parse_data_type_rule_recover(const Wrapper &wrapper,
                             const std::optional<Skipper> &localSkipper,
                             const grammar::AbstractRule *self,
                             RecoveryContext &ctx);

/// Expect-mode `DataTypeRule` body. Compiled once in RuleParseSupport.cpp.
[[nodiscard]] bool
parse_data_type_rule_expect(const Wrapper &wrapper,
                            const std::optional<Skipper> &localSkipper,
                            const grammar::AbstractRule *self,
                            ExpectContext &ctx);

/// Selects which of `ParserRule`'s three concept-mandated recovery probes
/// `run_rule_recovery_probe` runs.
enum class RuleRecoveryProbeKind : std::uint8_t {
  /// `probeRecoverable`: the wrapper's local-recovery probe, gated on the
  /// wrapper exposing one at all.
  Recoverable,
  /// `probeRecoverableAtEntry`: the strict fast probe first, then the
  /// wrapper's entry-recovery probe.
  AtEntry,
  /// `probeRecoverableAtEntryConsumesVisible`: the strict fast probe first,
  /// then the entry-recovery probe that must consume visible source.
  AtEntryConsumesVisible,
};

/// Shared body of `ParserRule`'s `probeRecoverable*` trio: the active-recovery
/// re-entry guard, the active-recovery scope, and the probe dispatch under the
/// rule's local skipper. Compiled once in RuleParseSupport.cpp.
[[nodiscard]] bool
run_rule_recovery_probe(const Wrapper &wrapper,
                        const std::optional<Skipper> &localSkipper,
                        const grammar::AbstractElement *self,
                        RecoveryContext &ctx, RuleRecoveryProbeKind kind);

} // namespace pegium::parser::detail
