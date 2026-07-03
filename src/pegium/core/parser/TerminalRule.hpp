#pragma once

/// Parser rule template producing terminal scalar values.
///
/// The strict-mode path (`terminal()`, and the strict branch of `parse_impl`)
/// is defined in-class below; it is the hot path exercised by clean input.
/// The value-conversion members (`convertValue` and its helpers) and the
/// remaining plumbing (constructors, `getValue`/`appendTextValue`,
/// `operator=`, `setValueConverter`) are mode-agnostic and stay in-class too.
/// Recovery- and expect-mode members (`terminal_recovery_profile`,
/// `parse_terminal_recovery_impl`, and `probeRecoverable` /
/// `probeRecoverableAtEntry` / `probeRecoverableAtEntryConsumesVisible`) are
/// declared here and defined out-of-class in TerminalRuleRecovery.hpp — one
/// file rather than a recovery/expect pair, because
/// `parse_terminal_recovery_impl` is a single template dispatching between
/// the two modes with `if constexpr`, not two separate members. A
/// translation unit that drives a TerminalRule through recovery or expect
/// mode must include that header in addition to this one.

#include <array>
#include <cassert>
#include <charconv>
#include <concepts>
#include <functional>
#include <optional>
#include <ranges>
#include <pegium/core/parser/AbstractRule.hpp>
#include <pegium/core/parser/DataTypeRuleSupport.hpp>
#include <pegium/core/parser/ExpectContext.hpp>
#include <pegium/core/parser/Introspection.hpp>
#include <pegium/core/parser/RecoveryContext.hpp>
#include <pegium/core/parser/ParseMode.hpp>
#include <pegium/core/parser/RecoveryCandidate.hpp>
#include <pegium/core/parser/RecoveryTrace.hpp>
#include <pegium/core/parser/RuleOptions.hpp>
#include <pegium/core/parser/RuleValue.hpp>
#include <pegium/core/parser/TerminalLocalRecovery.hpp>
#include <pegium/core/parser/TerminalRecoverySupport.hpp>
#include <pegium/core/parser/ValueBuildContext.hpp>
#include <string>

namespace pegium::parser {

template <Expression... Elements> struct Group;

template <typename T = std::string>
  requires detail::SupportedRuleValueType<T>
struct TerminalRule final : AbstractRule<grammar::TerminalRule> {
  using type = T;
  using value_variant = grammar::RuleValue;
  using BaseRule = AbstractRule<grammar::TerminalRule>;
  static constexpr bool isFailureSafe = true;

  template <NonNullableTerminalCapableExpression Element>
  constexpr TerminalRule(std::string_view name, Element &&element)
      : TerminalRule(name,
                     detail::infer_direct_literal_recovery_metadata(element),
                     std::forward<Element>(element)) {}

  template <NonNullableTerminalCapableExpression Element, typename... Options>
    requires(sizeof...(Options) > 0)
  constexpr TerminalRule(std::string_view name, Element &&element,
                         Options &&...options)
      : TerminalRule(name,
                     detail::infer_direct_literal_recovery_metadata(element),
                     std::forward<Element>(element)) {
    (applyOption(std::forward<Options>(options)), ...);
  }

  std::string_view getTypeName() const noexcept override {
    static constexpr auto typeName = detail::type_name_v<T>;
    return typeName;
  }

  const char *terminal(const char *begin) const noexcept {
    assert(this->_wrapper.has_terminal() &&
           "TerminalRule requires a terminal-capable wrapped expression");
    return this->_wrapper.try_terminal(begin);
  }

  const char *terminal(const std::string &text) const noexcept {
    return terminal(text.c_str());
  }

  /// Recovery-mode; defined out-of-class in TerminalRuleRecovery.hpp.
  bool probeRecoverable(RecoveryContext &ctx) const noexcept;

  /// Recovery-mode; defined out-of-class in TerminalRuleRecovery.hpp.
  bool probeRecoverableAtEntry(RecoveryContext &ctx) const noexcept;

  /// Recovery-mode; defined out-of-class in TerminalRuleRecovery.hpp.
  bool
  probeRecoverableAtEntryConsumesVisible(RecoveryContext &ctx) const noexcept;

  TerminalRule super() const { return *this; }
  inline T getRawValue(const CstNodeView &node) const {
    return convertValue(node, nullptr);
  }

  inline T getRawValue(const CstNodeView &node,
                       const ValueBuildContext &context) const {
    return convertValue(node, &context);
  }

  value_variant
  getValue(const CstNodeView &node,
           const ValueBuildContext *context = nullptr) const override {
    return detail::toRuleValue(convertValue(node, context));
  }

  void
  appendTextValue(std::string &out, const CstNodeView &node,
                  const ValueBuildContext *context = nullptr) const override {
    if (!_hasCustomValueConverter) {
      appendDefaultTextValue(out, node, context);
      return;
    }
    appendConvertedValue(out, convertValue(node, context));
  }

private:
  friend struct detail::ParseAccess;
  template <Expression... Elements> friend struct Group;
  template <typename> friend struct SkipperWrapped;

  template <NonNullableTerminalCapableExpression Element>
  constexpr TerminalRule(std::string_view name,
                         std::optional<detail::DirectLiteralRecoveryMetadata>
                             literalRecoveryMetadata,
                         Element &&element)
      : BaseRule(name, std::forward<Element>(element)),
        // Derive the shape from the same metadata the matcher reads (one
        // source of truth); `_terminalShape` is declared before
        // `_literalRecoveryMetadata`, so this reads the param before the move.
        _terminalShape(
            detail::terminal_shape_from_recovery_metadata(literalRecoveryMetadata)),
        _literalRecoveryMetadata(std::move(literalRecoveryMetadata)) {
    static_assert(
        detail::ConsistentLiteralRecoveryElement<std::remove_cvref_t<Element>>,
        "A terminal element exposing getValue() must also expose "
        "isCaseSensitive() so the recovery shape and matcher metadata stay "
        "consistent.");
  }

  detail::TerminalShape _terminalShape{};
  std::optional<detail::DirectLiteralRecoveryMetadata>
      _literalRecoveryMetadata{};

  /// Type-erased view of this rule consumed by the shared terminal
  /// local-recovery driver (TerminalLocalRecovery.cpp). The matcher
  /// trampoline is static per instantiation; the shape and direct-literal
  /// metadata are per instance, so the profile is built on the stack per
  /// call. Recovery-mode; defined out-of-class in TerminalRuleRecovery.hpp.
  [[nodiscard]] detail::TerminalRecoveryProfile
  terminal_recovery_profile() const noexcept;

  /// Recovery- and expect-mode (dispatched by `if constexpr` on `Context`);
  /// defined out-of-class in TerminalRuleRecovery.hpp.
  template <EditableParseModeContext Context>
  bool parse_terminal_recovery_impl(
      Context &ctx,
      const detail::TerminalRecoveryFacts &terminalRecoveryFacts) const;

  template <ParseModeContext Context> bool parse_impl(Context &ctx) const {
    if constexpr (StrictParseModeContext<Context>) {
      const char *const cursorStart = ctx.cursor();
      PEGIUM_RECOVERY_TRACE("[terminal rule] enter ", getName(),
                            " offset=", ctx.cursorOffset());
      if (const char *const matchedEnd = terminal(cursorStart);
          matchedEnd != nullptr) {
        PEGIUM_RECOVERY_TRACE("[terminal rule] direct match ", getName(),
                              " offset=", ctx.cursorOffset());
        ctx.leaf(matchedEnd, this);
        return true;
      }
      PEGIUM_RECOVERY_TRACE("[terminal rule] strict fail ", getName(),
                            " offset=", ctx.cursorOffset());
      return false;
    } else {
      return parse_terminal_recovery_impl(ctx, {});
    }
  }

public:
  template <NonNullableTerminalCapableExpression Element>
  TerminalRule &operator=(Element &&element) {
    static_assert(
        detail::ConsistentLiteralRecoveryElement<std::remove_cvref_t<Element>>,
        "A terminal element exposing getValue() must also expose "
        "isCaseSensitive() so the recovery shape and matcher metadata stay "
        "consistent.");
    _literalRecoveryMetadata =
        detail::infer_direct_literal_recovery_metadata(element);
    _terminalShape =
        detail::terminal_shape_from_recovery_metadata(_literalRecoveryMetadata);
    BaseRule::operator=(std::forward<Element>(element));
    return *this;
  }
  void
  setValueConverter(std::function<opt::ConversionResult<T>(std::string_view)>
                        &&value_converter) {
    _value_converter = std::move(value_converter);
    _hasCustomValueConverter = static_cast<bool>(_value_converter);
  }

private:
  std::function<opt::ConversionResult<T>(std::string_view)> _value_converter;
  bool _hasCustomValueConverter = false;

  static constexpr std::string_view defaultConversionFailureMessage() noexcept {
    return "Conversion failed";
  }

  void reportConversionFailure(const CstNodeView &node,
                               const ValueBuildContext *context,
                               std::string_view message) const {
    detail::report_conversion_failure(node, this, context,
                                      std::string(message));
  }

  template <typename V>
  static void appendConvertedValue(std::string &out, V &&value) {
    using RawV = std::remove_cvref_t<V>;

    if constexpr (std::same_as<RawV, std::string_view>) {
      out.append(value);
    } else if constexpr (std::same_as<RawV, std::string>) {
      out.append(value);
    } else if constexpr (std::same_as<RawV, bool>) {
      out.append(value ? "true" : "false");
    } else if constexpr (std::same_as<RawV, std::nullptr_t>) {
      out.append("null");
    } else if constexpr (std::same_as<RawV, char>) {
      out += std::to_string(static_cast<int>(value));
    } else if constexpr (std::is_enum_v<RawV>) {
      appendConvertedValue(out,
                           static_cast<std::underlying_type_t<RawV>>(value));
    } else if constexpr (std::integral<RawV> && !std::same_as<RawV, bool> &&
                         !std::same_as<RawV, char>) {
      std::array<char, 32> buffer{};
      const auto [ptr, ec] =
          std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
      if (ec == std::errc()) {
        out.append(buffer.data(), ptr);
      }
    } else if constexpr (std::floating_point<RawV>) {
      out += std::to_string(value);
    }
  }

  [[nodiscard]] static constexpr bool
  is_zero_width_recovered_leaf(const CstNodeView &node) noexcept {
    return node.isRecovered() && node.getBegin() == node.getEnd();
  }

  [[nodiscard]] std::string_view
  text_value(const CstNodeView &node) const noexcept {
    if (node.isRecovered() && _literalRecoveryMetadata.has_value()) {
      return _literalRecoveryMetadata->value;
    }
    return node.getText();
  }

  T convertDefaultValue(const CstNodeView &node,
                        const ValueBuildContext *context) const {
    if (is_zero_width_recovered_leaf(node)) {
      return {};
    }
    const auto text = text_value(node);
    if constexpr (std::is_same_v<T, std::string_view>) {
      return text;
    } else if constexpr (std::is_same_v<T, std::string>) {
      return std::string(text);
    } else if constexpr (std::is_same_v<T, bool>) {
      return text == "true";
    } else if constexpr ((std::is_integral_v<T> ||
                          std::is_floating_point_v<T>) &&
                         !std::is_same_v<T, bool> && !std::is_same_v<T, char>) {
      T value{};
      const auto [ptr, ec] =
          std::from_chars(text.data(), text.data() + text.size(), value);
      if (ec == std::errc() && ptr == text.data() + text.size()) {
        return value;
      }
      reportConversionFailure(node, context, defaultConversionFailureMessage());
      return {};
    } else {
      reportConversionFailure(node, context,
                              detail::missing_converter_message(getName()));
      return {};
    }
  }

  void appendDefaultTextValue(std::string &out, const CstNodeView &node,
                              const ValueBuildContext *context) const {
    if constexpr (std::same_as<T, std::string_view> ||
                  std::same_as<T, std::string>) {
      const auto text = text_value(node);
      out.append(text.data(), text.size());
    } else {
      appendConvertedValue(out, convertDefaultValue(node, context));
    }
  }

  T convertValue(const CstNodeView &node,
                 const ValueBuildContext *context) const {
    if (is_zero_width_recovered_leaf(node)) {
      return {};
    }
    if (!_hasCustomValueConverter) {
      return convertDefaultValue(node, context);
    }
    const auto text = text_value(node);
    return detail::finish_conversion<T>(_value_converter(text), getName(),
                                        this, node, context);
  }

  template <typename Option> void applyOption(Option &&option) {
    using OptionType = std::remove_cvref_t<Option>;
    if constexpr (opt::IsConverterOption_v<OptionType>) {
      setValueConverterFromOption(std::forward<Option>(option).converter);
    } else {
      static_assert(opt::detail::DependentFalse_v<OptionType>,
                    "Unsupported option for TerminalRule. "
                    "Supported option: opt::with_converter(...).");
    }
  }

  template <typename Converter>
  void setValueConverterFromOption(Converter &&converter) {
    using ConverterType = std::remove_cvref_t<Converter>;
    static_assert(
        std::is_nothrow_invocable_v<ConverterType &, std::string_view>,
        "TerminalRule converter must be noexcept and invocable with "
        "std::string_view.");

    // The converter may return opt::ConversionResult<T> (fallible) or a value
    // convertible to T directly (infallible); as_conversion_result normalizes
    // both and statically rejects anything else.
    _value_converter =
        detail::normalize_text_converter<T>(std::forward<Converter>(converter));
    _hasCustomValueConverter = true;
  }
};

namespace detail {

template <typename T> struct IsTerminalRule : std::false_type {};

template <typename T>
struct IsTerminalRule<TerminalRule<T>> : std::true_type {};

template <typename T>
inline constexpr bool IsTerminalRule_v =
    IsTerminalRule<std::remove_cvref_t<T>>::value;

} // namespace detail

} // namespace pegium::parser
