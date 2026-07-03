#pragma once
/// Parser rule template producing scalar values.
#include <optional>
#include <pegium/core/grammar/DataTypeRule.hpp>
#include <pegium/core/parser/AbstractRule.hpp>
#include <pegium/core/parser/CompletionSupport.hpp>
#include <pegium/core/parser/DataTypeRuleSupport.hpp>
#include <pegium/core/parser/Introspection.hpp>
#include <pegium/core/parser/ParseMode.hpp>
#include <pegium/core/parser/RecoveryContext.hpp>
#include <pegium/core/parser/RuleOptions.hpp>
#include <pegium/core/parser/RuleParseSupport.hpp>
#include <pegium/core/parser/RuleValue.hpp>
#include <pegium/core/parser/Skipper.hpp>
#include <pegium/core/parser/ValueBuildContext.hpp>
#include <string>
#include <string_view>
namespace pegium::parser {

template <typename T = std::string, bool Nullable = false>
  requires(!std::derived_from<T, AstNode>) && detail::SupportedRuleValueType<T>
struct DataTypeRule final : AbstractRule<grammar::DataTypeRule, Nullable>,
                            CompletionSkipperProvider {
  using type = T;
  using value_variant = grammar::RuleValue;
  using BaseRule = AbstractRule<grammar::DataTypeRule, Nullable>;
  static constexpr bool isFailureSafe = false;
  using BaseRule::BaseRule;
  // Base is dependent on `Nullable`; re-export the protected members we
  // touch in this header.
  using BaseRule::_wrapper;

  template <Expression Element, typename... Options>
    requires(sizeof...(Options) > 0) &&
            (std::remove_cvref_t<Element>::nullable == Nullable)
  constexpr DataTypeRule(std::string_view name, Element &&element,
                         Options &&...options)
      : BaseRule(name, std::forward<Element>(element)) {
    (applyOption(std::forward<Options>(options)), ...);
  }

  DataTypeRule super() const { return *this; }

  std::string_view getTypeName() const noexcept override {
    static constexpr auto typeName = detail::type_name_v<T>;
    return typeName;
  }
  T getRawValue(const CstNodeView &node) const {
    return convertValue(node, nullptr);
  }

  T getRawValue(const CstNodeView &node,
                const ValueBuildContext &context) const {
    return convertValue(node, &context);
  }

  value_variant getValue(const CstNodeView &node,
                         const ValueBuildContext *context = nullptr) const override {
    return detail::toRuleValue(convertValue(node, context));
  }

  [[nodiscard]] const Skipper *
  getCompletionSkipper() const noexcept override {
    return _localSkipper.has_value() ? std::addressof(*_localSkipper) : nullptr;
  }

  using BaseRule::operator=;

private:
  friend struct detail::ParseAccess;

  std::optional<Skipper> _localSkipper;
  std::function<opt::ConversionResult<T>(const CstNodeView &,
                                         const ValueBuildContext *)>
      _value_converter;
  bool _hasCustomValueConverter = false;
  using ValueSupport = detail::DataTypeRuleValueSupport<T>;

  // The parse bodies are rule-value-type-free; they live in
  // RuleParseSupport.{hpp,cpp} and see this rule only as its type-erased
  // wrapper, its local skipper and its grammar identity.
  template <ParseModeContext Context> bool parse_impl(Context &ctx) const {
    if constexpr (StrictParseModeContext<Context>) {
      return detail::parse_data_type_rule_strict(_wrapper, _localSkipper, this,
                                                 ctx);
    } else if constexpr (RecoveryParseModeContext<Context>) {
      return detail::parse_data_type_rule_recover(_wrapper, _localSkipper,
                                                  this, ctx);
    } else {
      return detail::parse_data_type_rule_expect(_wrapper, _localSkipper, this,
                                                 ctx);
    }
  }

  T convertValue(const CstNodeView &node,
                 const ValueBuildContext *context) const {
    return ValueSupport::convert_value(*this, node, context,
                                       _hasCustomValueConverter,
                                       _value_converter);
  }

  template <typename Option> void applyOption(Option &&option) {
    using OptionType = std::remove_cvref_t<Option>;
    if constexpr (opt::IsSkipperOption_v<OptionType>) {
      _localSkipper = std::forward<Option>(option).skipper;
    } else if constexpr (opt::IsConverterOption_v<OptionType>) {
      setValueConverterFromOption(std::forward<Option>(option).converter);
    } else {
      static_assert(opt::detail::DependentFalse_v<OptionType>,
                    "Unsupported option for DataTypeRule. "
                    "Supported options: opt::with_skipper(...), "
                    "opt::with_converter(...).");
    }
  }

  template <typename Converter>
  void setValueConverterFromOption(Converter &&converter) {
    ValueSupport::set_value_converter_from_option(
        _value_converter, _hasCustomValueConverter,
        std::forward<Converter>(converter));
  }
};

} // namespace pegium::parser
