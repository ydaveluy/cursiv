#include <pegium/core/parser/RuleParseSupport.hpp>

#include <algorithm>

#include <pegium/core/parser/ContextShared.hpp>
#include <pegium/core/parser/ExpectContext.hpp>
#include <pegium/core/parser/RecoveryTrace.hpp>

namespace pegium::parser::detail {

bool parse_named_rule_recover(const Wrapper &wrapper,
                              const std::optional<Skipper> &localSkipper,
                              const grammar::AbstractRule *self,
                              RecoveryContext &ctx) {
  if (ctx.recoveryDescentInactive()) {
    return parse_named_rule_strict(wrapper, localSkipper, self,
                                   static_cast<TrackedParseContext &>(ctx));
  }
  // Pathological grammar shapes (e.g. unclosed nested call expressions)
  // can drive `evaluate_editable_recovery_candidate` into an
  // exponentially-branching tree of speculative parses. Cap the
  // cumulative ParserRule recovery entries within the current window
  // and abort fast once the budget is gone — the outer recovery
  // driver then keeps whatever candidate was already in hand instead
  // of running for seconds. Counter is monotonic across rewinds: the
  // cap bounds the *sum* of speculative work, not per-branch work,
  // and the speculative path that hits it cannot be unwound without
  // letting siblings re-exponentiate. Each `try_recovery_window`
  // constructs a fresh `RecoveryContext`, which resets the counter
  // for free between windows. Cap check runs before the active-set
  // membership check so once the budget is gone every subsequent
  // entry fails in O(1) without scanning the active recovery stack.
  if (ctx.recoveryRuleEntries >= ctx.maxRecoveryRuleEntries) {
    return false;
  }
  if (ctx.isActiveRecovery(self)) {
    PEGIUM_RECOVERY_TRACE("[rule recover] recursive same-offset bail ",
                          self->getName(), " offset=", ctx.cursorOffset());
    return false;
  }
  // Stack-depth cap. Adversarial inputs can drive the recovery
  // descent into deep grammar nesting (e.g. recursive container
  // types in adversarial graph fuzz seeds); ASan / sancov inflate
  // each recovery frame so the FuzzTest 128 KiB budget trips on
  // ~30 levels even though the parse is otherwise bounded. Cap
  // depth at ParserRule entry — strict and fast paths above are
  // unaffected.
  if (ctx.recoveryRuleDepth >= RecoveryContext::kMaxRecoveryRuleDepth) {
    return false;
  }
  ++ctx.recoveryRuleDepth;
  ScopedDecrementOnExit recoveryRuleDepthGuard{ctx.recoveryRuleDepth};
  (void)recoveryRuleDepthGuard;
  ++ctx.recoveryRuleEntries;
  auto activeRecoveryGuard = ctx.enterActiveRecovery(self);
  (void)activeRecoveryGuard;
  PEGIUM_RECOVERY_TRACE("[rule recover] enter ", self->getName(),
                        " offset=", ctx.cursorOffset(),
                        " allowI=", ctx.editPolicy.allowInsert,
                        " allowD=", ctx.editPolicy.allowDelete);
  const auto nodeStartCheckpoint = ctx.enter();
  const bool matched = run_under_local_skipper(
      localSkipper, ctx, [&] { return parse(wrapper, ctx); });
  if (!matched) {
    bool canCommitPartialTopLevelRecovery = false;
    if (ctx.activeRecoveryDepth() == 1 &&
        ctx.hasHadEdits() && !ctx.hasPendingCommittedRecoveryEdits()) {
      TextOffset lastEditOffset = 0;
      for (const auto &edit : ctx.recoveryEditsView()) {
        lastEditOffset = std::max(lastEditOffset, edit.endOffset);
      }
      const bool currentWindowContributed =
          !ctx.hasPendingRecoveryWindows() ||
          ctx.completedRecoveryWindowCount() > 0u ||
          lastEditOffset >= ctx.pendingRecoveryWindowBeginOffset();
      const auto partialRecoveryFloor =
          std::max(lastEditOffset, ctx.pendingRecoveryWindowBeginOffset());
      canCommitPartialTopLevelRecovery =
          currentWindowContributed &&
          ctx.maxCursorOffset() > partialRecoveryFloor;
    }
    if (canCommitPartialTopLevelRecovery) {
      ctx.exit(nodeStartCheckpoint, self);
      return true;
    }
    PEGIUM_RECOVERY_TRACE("[rule recover] fail ", self->getName(),
                          " offset=", ctx.cursorOffset());
    return false;
  }
  PEGIUM_RECOVERY_TRACE("[rule recover] ok ", self->getName(),
                        " offset=", ctx.cursorOffset(),
                        " hadEdits=", ctx.hasHadEdits());
  ctx.exit(nodeStartCheckpoint, self);
  utils::throw_if_cancelled(ctx.cancellationToken());
  return true;
}

bool parse_named_rule_expect(const Wrapper &wrapper,
                             const std::optional<Skipper> &localSkipper,
                             const grammar::AbstractRule *self,
                             ExpectContext &ctx) {
  if (ctx.isActiveRecovery(self)) {
    return false;
  }
  auto activeRecoveryGuard = ctx.enterActiveRecovery(self);
  (void)activeRecoveryGuard;
  auto ruleGuard = ctx.with_rule(self);
  (void)ruleGuard;
  const auto nodeStartCheckpoint = ctx.enter();
  const bool matched = run_under_local_skipper(
      localSkipper, ctx, [&] { return parse(wrapper, ctx); });
  if (!matched) {
    return false;
  }
  ctx.exit(nodeStartCheckpoint, self);
  // ExpectContext::exit already invokes `throw_if_cancelled`; this
  // branch (grammar analysis path) is not on the hot strict path so
  // we don't bother throttling it.
  return true;
}

bool parse_data_type_rule_recover(const Wrapper &wrapper,
                                  const std::optional<Skipper> &localSkipper,
                                  const grammar::AbstractRule *self,
                                  RecoveryContext &ctx) {
  if (ctx.recoveryDescentInactive()) {
    return parse_data_type_rule_strict(wrapper, localSkipper, self,
                                       static_cast<TrackedParseContext &>(ctx));
  }
  if (ctx.isActiveRecovery(self)) {
    return false;
  }
  auto activeRecoveryGuard = ctx.enterActiveRecovery(self);
  (void)activeRecoveryGuard;
  const auto nodeStartCheckpoint = ctx.enter();
  const bool matched = run_under_local_skipper(
      localSkipper, ctx, [&] { return parse(wrapper, ctx); });
  if (!matched) {
    return false;
  }
  ctx.exit(nodeStartCheckpoint, self);
  return true;
}

bool parse_data_type_rule_expect(const Wrapper &wrapper,
                                 const std::optional<Skipper> &localSkipper,
                                 const grammar::AbstractRule *self,
                                 ExpectContext &ctx) {
  if (ctx.reachedAnchor()) {
    ctx.addRule(self);
    return true;
  }
  if (ctx.isActiveRecovery(self)) {
    return false;
  }
  auto activeRecoveryGuard = ctx.enterActiveRecovery(self);
  (void)activeRecoveryGuard;
  const auto nodeStartCheckpoint = ctx.enter();
  const bool matched = run_under_local_skipper(
      localSkipper, ctx, [&] { return parse(wrapper, ctx); });
  if (!matched) {
    return false;
  }
  ctx.exit(nodeStartCheckpoint, self);
  return true;
}

bool run_rule_recovery_probe(const Wrapper &wrapper,
                             const std::optional<Skipper> &localSkipper,
                             const grammar::AbstractElement *self,
                             RecoveryContext &ctx,
                             RuleRecoveryProbeKind kind) {
  if (kind == RuleRecoveryProbeKind::Recoverable &&
      !wrapper.has_recovery_probe()) {
    return false;
  }
  if (ctx.isActiveRecovery(self)) {
    return false;
  }
  auto activeRecoveryGuard = ctx.enterActiveRecovery(self);
  (void)activeRecoveryGuard;
  const auto probe = [&] {
    switch (kind) {
    case RuleRecoveryProbeKind::Recoverable:
      return wrapper.probe_recoverable(ctx);
    case RuleRecoveryProbeKind::AtEntry:
      if (wrapper.fast_probe(ctx)) {
        return true;
      }
      return wrapper.probe_recoverable_at_entry(ctx);
    case RuleRecoveryProbeKind::AtEntryConsumesVisible:
      if (wrapper.fast_probe(ctx)) {
        return true;
      }
      return wrapper.probe_recoverable_at_entry_consumes_visible(ctx);
    }
    return false;
  };
  return run_under_local_skipper(localSkipper, ctx, probe);
}

} // namespace pegium::parser::detail
