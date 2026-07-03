#pragma once
/// Parser rule template producing concrete AST nodes.
#include <concepts>
#include <optional>
#include <pegium/core/parser/AbstractRule.hpp>
#include <pegium/core/parser/CompletionSupport.hpp>
#include <pegium/core/parser/ExpectContext.hpp>
#include <pegium/core/parser/Introspection.hpp>
#include <pegium/core/parser/ParseAttempt.hpp>
#include <pegium/core/parser/ParserRuleSupport.hpp>
#include <pegium/core/parser/ParseMode.hpp>
#include <pegium/core/parser/RecoveryContext.hpp>
#include <pegium/core/parser/RuleOptions.hpp>
#include <pegium/core/parser/RuleParseSupport.hpp>
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
    return detail::run_rule_recovery_probe(
        _wrapper, _localSkipper, this, ctx,
        detail::RuleRecoveryProbeKind::Recoverable);
  }

  bool probeRecoverableAtEntry(RecoveryContext &ctx) const {
    return detail::run_rule_recovery_probe(
        _wrapper, _localSkipper, this, ctx,
        detail::RuleRecoveryProbeKind::AtEntry);
  }

  bool probeRecoverableAtEntryConsumesVisible(RecoveryContext &ctx) const {
    return detail::run_rule_recovery_probe(
        _wrapper, _localSkipper, this, ctx,
        detail::RuleRecoveryProbeKind::AtEntryConsumesVisible);
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

  template <StrictParseModeContext Context>
  bool fast_probe_impl(Context &ctx) const {
    return detail::run_under_local_skipper(
        _localSkipper, ctx, [&] { return _wrapper.fast_probe(ctx); });
  }

  // The parse bodies are rule-value-type-free; they live in
  // RuleParseSupport.{hpp,cpp} and see this rule only as its type-erased
  // wrapper, its local skipper and its grammar identity.
  template <ParseModeContext Context> bool parse_impl(Context &ctx) const {
    if constexpr (StrictParseModeContext<Context>) {
      return detail::parse_named_rule_strict(_wrapper, _localSkipper, this,
                                             ctx);
    } else if constexpr (RecoveryParseModeContext<Context>) {
      return detail::parse_named_rule_recover(_wrapper, _localSkipper, this,
                                              ctx);
    } else {
      return detail::parse_named_rule_expect(_wrapper, _localSkipper, this,
                                             ctx);
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
