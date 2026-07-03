#pragma once
/// Parser rule template producing concrete AST nodes.
#include <algorithm>
#include <concepts>
#include <optional>
#include <pegium/core/parser/AbstractRule.hpp>
#include <pegium/core/parser/CompletionSupport.hpp>
#include <pegium/core/parser/ExpectContext.hpp>
#include <pegium/core/parser/Introspection.hpp>
#include <pegium/core/parser/ParseAttempt.hpp>
#include <pegium/core/parser/Parser.hpp>
#include <pegium/core/parser/ParserRuleSupport.hpp>
#include <pegium/core/parser/ParseMode.hpp>
#include <pegium/core/parser/ParseContext.hpp>
#include <pegium/core/parser/RecoveryTrace.hpp>
#include <pegium/core/parser/RuleOptions.hpp>
#include <pegium/core/parser/Skipper.hpp>
#include <pegium/core/parser/AstReflectionBootstrap.hpp>
#include <pegium/core/parser/ValueBuildContext.hpp>
#include <string_view>

namespace pegium::parser {

template <typename T, bool Nullable = false>
  requires DefaultConstructibleAstNode<T>
struct ParserRule final : AbstractRule<grammar::ParserRule, Nullable>,
                          CompletionSkipperProvider {
  using type = T;
  using BaseRule = AbstractRule<grammar::ParserRule, Nullable>;
  static constexpr bool isFailureSafe = false;
  using BaseRule::BaseRule;
  // The base is dependent (Nullable is a template parameter), so unqualified
  // member lookup does not see protected members from it. Re-export the few
  // we touch in this header.
  using BaseRule::_wrapper;

  template <Expression Element, typename... Options>
    requires(sizeof...(Options) > 0) &&
            (std::remove_cvref_t<Element>::nullable == Nullable)
  constexpr ParserRule(std::string_view name, Element &&element,
                       Options &&...options)
      : BaseRule(name, std::forward<Element>(element)) {
    (applyOption(std::forward<Options>(options)), ...);
  }

  ParserRule super() const { return *this; }

  std::string_view getTypeName() const noexcept override {
    static constexpr auto typeName = detail::type_name_v<T>;
    return typeName;
  }

  AstNode *getValue(const CstNodeView &node,
                    const ValueBuildContext &context) const override {
    return getRawValue(node, context);
  }

  T *getRawValue(const CstNodeView &node,
                 const ValueBuildContext &context) const {
    return detail::ParserRuleBuildSupport<T>::get_raw_value(node, context);
  }

  bool rule(ParseContext &ctx) const override { return parse_impl(ctx); }
  bool rule(TrackedParseContext &ctx) const override { return parse_impl(ctx); }
  bool recover(RecoveryContext &ctx) const override { return parse_impl(ctx); }
  bool expect(ExpectContext &ctx) const override { return parse_impl(ctx); }
  void init(AstReflectionInitContext &ctx) const override { init_impl(ctx); }
  bool probeRecoverable(RecoveryContext &ctx) const {
    if (!_wrapper.has_recovery_probe()) {
      return false;
    }
    if (ctx.isActiveRecovery(this)) {
      return false;
    }
    auto activeRecoveryGuard = ctx.enterActiveRecovery(this);
    (void)activeRecoveryGuard;
    return run_under_local_skipper(
        ctx, [&] { return _wrapper.probe_recoverable(ctx); });
  }

  bool probeRecoverableAtEntry(RecoveryContext &ctx) const {
    if (ctx.isActiveRecovery(this)) {
      return false;
    }
    auto activeRecoveryGuard = ctx.enterActiveRecovery(this);
    (void)activeRecoveryGuard;
    const auto probeAtEntry = [this, &ctx]() {
      if (_wrapper.fast_probe(ctx)) {
        return true;
      }
      return _wrapper.probe_recoverable_at_entry(ctx);
    };
    return run_under_local_skipper(ctx, probeAtEntry);
  }

  bool probeRecoverableAtEntryConsumesVisible(RecoveryContext &ctx) const {
    if (ctx.isActiveRecovery(this)) {
      return false;
    }
    auto activeRecoveryGuard = ctx.enterActiveRecovery(this);
    (void)activeRecoveryGuard;
    const auto probeAtEntry = [this, &ctx]() {
      if (_wrapper.fast_probe(ctx)) {
        return true;
      }
      return _wrapper.probe_recoverable_at_entry_consumes_visible(ctx);
    };
    return run_under_local_skipper(ctx, probeAtEntry);
  }

  [[nodiscard]] const Skipper *
  getCompletionSkipper() const noexcept override {
    return _localSkipper.has_value() ? std::addressof(*_localSkipper) : nullptr;
  }
  using BaseRule::operator=;

private:
  friend struct detail::ParseAccess;
  friend struct detail::FastProbeAccess;
  friend struct detail::InitAccess;

  std::optional<Skipper> _localSkipper;

  // Runs `fn` with the rule's optional local skipper installed for its
  // duration. The skipper guard is released as `fn` returns, before any
  // surrounding `ctx.exit`.
  template <typename Context, typename Fn>
  decltype(auto) run_under_local_skipper(Context &ctx, Fn &&fn) const {
    if (_localSkipper.has_value()) {
      auto localSkipperGuard = ctx.with_skipper(*_localSkipper);
      (void)localSkipperGuard;
      return std::forward<Fn>(fn)();
    }
    return std::forward<Fn>(fn)();
  }

  template <StrictParseModeContext Context>
  bool fast_probe_impl(Context &ctx) const {
    return run_under_local_skipper(
        ctx, [&] { return _wrapper.fast_probe(ctx); });
  }

  template <ParseModeContext Context> bool parse_impl(Context &ctx) const {
    if constexpr (StrictParseModeContext<Context>) {
      PEGIUM_RECOVERY_TRACE("[rule rule] enter ", getName(),
                            " offset=", ctx.cursorOffset());
      const auto nodeStartCheckpoint = ctx.enter();
      bool matched = false;
      if (_localSkipper.has_value()) {
        auto localSkipperGuard = ctx.with_skipper(*_localSkipper);
        (void)localSkipperGuard;
        matched = parse(_wrapper, ctx);
      } else {
        matched = parse(_wrapper, ctx);
      }
      if (!matched) {
        PEGIUM_RECOVERY_TRACE("[rule rule] fail ", getName(),
                              " offset=", ctx.cursorOffset());
        return false;
      }
      ctx.exit(nodeStartCheckpoint, this);
      utils::throw_if_cancelled(ctx.cancellationToken());
      PEGIUM_RECOVERY_TRACE("[rule rule] ok ", getName(),
                            " offset=", ctx.cursorOffset());
      return true;
    } else if constexpr (RecoveryParseModeContext<Context>) {
      if (ctx.recoveryDescentInactive()) {
        return parse_impl(static_cast<TrackedParseContext &>(ctx));
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
      if (ctx.isActiveRecovery(this)) {
        PEGIUM_RECOVERY_TRACE("[rule recover] recursive same-offset bail ",
                              getName(), " offset=", ctx.cursorOffset());
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
      detail::ScopedDecrementOnExit recoveryRuleDepthGuard{
          ctx.recoveryRuleDepth};
      (void)recoveryRuleDepthGuard;
      ++ctx.recoveryRuleEntries;
      auto activeRecoveryGuard = ctx.enterActiveRecovery(this);
      (void)activeRecoveryGuard;
      PEGIUM_RECOVERY_TRACE(
          "[rule recover] enter ", getName(), " offset=", ctx.cursorOffset(),
          " allowI=", ctx.allowInsert, " allowD=", ctx.allowDelete);
      const auto nodeStartCheckpoint = ctx.enter();
      bool matched = false;
      if (_localSkipper.has_value()) {
        auto localSkipperGuard = ctx.with_skipper(*_localSkipper);
        (void)localSkipperGuard;
        matched = parse(_wrapper, ctx);
      } else {
        matched = parse(_wrapper, ctx);
      }
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
          ctx.exit(nodeStartCheckpoint, this);
          return true;
        }
        PEGIUM_RECOVERY_TRACE("[rule recover] fail ", getName(),
                              " offset=", ctx.cursorOffset());
        return false;
      }
      PEGIUM_RECOVERY_TRACE("[rule recover] ok ", getName(),
                            " offset=", ctx.cursorOffset(),
                            " hadEdits=", ctx.hasHadEdits());
      ctx.exit(nodeStartCheckpoint, this);
      utils::throw_if_cancelled(ctx.cancellationToken());
      return true;
    } else {
      if (ctx.isActiveRecovery(this)) {
        return false;
      }
      auto activeRecoveryGuard = ctx.enterActiveRecovery(this);
      (void)activeRecoveryGuard;
      auto ruleGuard = ctx.with_rule(this);
      (void)ruleGuard;
      const auto nodeStartCheckpoint = ctx.enter();
      bool matched = false;
      if (_localSkipper.has_value()) {
        auto localSkipperGuard = ctx.with_skipper(*_localSkipper);
        (void)localSkipperGuard;
        matched = parse(_wrapper, ctx);
      } else {
        matched = parse(_wrapper, ctx);
      }
      if (!matched) {
        return false;
      }
      ctx.exit(nodeStartCheckpoint, this);
      // ExpectContext::exit already invokes `throw_if_cancelled`; this
      // branch (grammar analysis path) is not on the hot strict path so
      // we don't bother throttling it.
      return true;
    }
  }

  template <typename Option> void applyOption(Option &&option) {
    using OptionType = std::remove_cvref_t<Option>;
    if constexpr (opt::IsSkipperOption_v<OptionType>) {
      _localSkipper = std::forward<Option>(option).skipper;
    } else {
      static_assert(opt::detail::DependentFalse_v<OptionType>,
                    "Unsupported option for ParserRule. "
                    "Supported option: opt::with_skipper(...).");
    }
  }

  void init_impl(AstReflectionInitContext &ctx) const {
    const auto &typeInfo = detail::ast_node_type_info<T>();
    ctx.registerProducedType(typeInfo);
    auto childContext = ctx.withExpectedType(typeInfo.type);
    this->_wrapper.init(childContext);
  }
};

namespace detail {

template <typename T> struct IsParserRule : std::false_type {};

template <typename T, bool Nullable>
struct IsParserRule<ParserRule<T, Nullable>> : std::true_type {};

} // namespace detail

template <typename T>
concept IsParserRule = detail::IsParserRule<std::remove_cvref_t<T>>::value;

} // namespace pegium::parser
