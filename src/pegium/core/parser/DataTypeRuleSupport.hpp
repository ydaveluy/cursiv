#pragma once

/// Conversion helpers backing parser data-type rules.

#include <cassert>
#include <functional>
#include <pegium/core/grammar/AnyCharacter.hpp>
#include <pegium/core/grammar/CharacterRange.hpp>
#include <pegium/core/grammar/DataTypeRule.hpp>
#include <pegium/core/grammar/Literal.hpp>
#include <pegium/core/grammar/TerminalRule.hpp>
#include <pegium/core/parser/RuleOptions.hpp>
#include <pegium/core/parser/ValueBuildContext.hpp>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace pegium::parser::detail {

/// Message shown when a rule's converter reports failure without its own
/// error text.
[[nodiscard]] inline std::string
generic_conversion_error_message(std::string_view ruleName) {
  return "Converter failed for rule " + std::string(ruleName);
}

/// Message shown when a rule's value type has no default conversion and no
/// converter was configured for it.
[[nodiscard]] inline std::string
missing_converter_message(std::string_view ruleName) {
  return "ValueConvert not provided for rule " + std::string(ruleName);
}

/// Records a conversion failure against the build context, when one is
/// tracking diagnostics.
inline void report_conversion_failure(const CstNodeView &node,
                                      const grammar::AbstractElement *element,
                                      const ValueBuildContext *context,
                                      std::string message) {
  if (context == nullptr) {
    return;
  }
  context->addConversionDiagnostic(node, element, std::move(message));
}

/// Unwraps a converter's result: the converted value on success, or a
/// reported diagnostic (the converter's own error, falling back to a
/// generic message when it left none) followed by a value-initialized
/// default.
template <typename T>
[[nodiscard]] T finish_conversion(opt::ConversionResult<T> &&result,
                                  std::string_view ruleName,
                                  const grammar::AbstractElement *element,
                                  const CstNodeView &node,
                                  const ValueBuildContext *context) {
  if (result.has_value()) {
    return std::move(result).value();
  }
  report_conversion_failure(
      node, element, context,
      result.error().empty() ? generic_conversion_error_message(ruleName)
                             : std::string(result.error()));
  return {};
}

/// Wraps a text-invocable converter into the `(std::string_view) ->
/// ConversionResult<T>` shape every rule's converter storage normalizes to:
/// a converter may return opt::ConversionResult<T> (fallible) or a value
/// convertible to T directly (infallible); as_conversion_result normalizes
/// both.
template <typename T, typename Converter>
[[nodiscard]] auto normalize_text_converter(Converter &&converter) {
  return [converterFn = std::forward<Converter>(converter)](
             std::string_view text) mutable -> opt::ConversionResult<T> {
    return opt::as_conversion_result<T>(std::invoke(converterFn, text));
  };
}

template <typename T> struct DataTypeRuleValueSupport {
  static void append_text_fragment(std::string &out,
                                   std::string_view text) {
    out.append(text.data(), text.size());
  }

  [[nodiscard]] static std::string
  unsupported_element_conversion_message(grammar::ElementKind kind) {
    return "a string data-type rule can only concatenate literals, character "
           "ranges, terminals and string-valued data-type rules (got element "
           "kind " +
           std::to_string(static_cast<int>(kind)) +
           "); flatten it to those, or provide a with_converter(...)";
  }

  template <typename Rule>
  static T convert_default_value(const Rule &rule, const CstNodeView &node,
                                 const ValueBuildContext *context) {
    if constexpr (std::is_same_v<T, std::string>) {
      std::string value;
      value.reserve(node.getText().size());
      for (const auto it : node) {
        const auto &child = it.node();
        if (child.isHidden) {
          continue;
        }

        const auto *grammarElement = child.grammarElement;
        assert(grammarElement);

        using enum grammar::ElementKind;
        switch (grammarElement->getKind()) {
        case Literal:
          // Use the canonical literal value: on a recovered/synthesized keyword
          // node getText() is empty (inserted) or the typo'd input (fuzzy
          // replace), whereas getValue(node) yields the grammar keyword and
          // falls back to getText() for non-recovered nodes.
          append_text_fragment(
              value,
              static_cast<const grammar::Literal *>(grammarElement)->getValue(it));
          break;
        case CharacterRange:
        case AnyCharacter:
          append_text_fragment(value, it.getText());
          break;
        case TerminalRule:
          static_cast<const grammar::TerminalRule *>(grammarElement)
              ->appendTextValue(value, it, context);
          break;
        case DataTypeRule: {
          // A nested data-type rule composes: append its own converted string,
          // built recursively through the grammar getValue (which honours the
          // sub-rule's converter). Lets a rule like `Modifiers = Visibility | Flags`
          // concatenate the values of its sub-rules.
          const auto nested =
              static_cast<const grammar::DataTypeRule *>(grammarElement)
                  ->getValue(it, context);
          if (const auto *str = std::get_if<std::string>(&nested)) {
            append_text_fragment(value, *str);
          } else if (const auto *view = std::get_if<std::string_view>(&nested)) {
            append_text_fragment(value, *view);
          } else {
            report_conversion_failure(node, std::addressof(rule), context,
                                      "a string data-type rule can only "
                                      "concatenate string-valued sub-rules");
            return {};
          }
          break;
        }
        default:
          report_conversion_failure(
              node, std::addressof(rule), context,
              unsupported_element_conversion_message(grammarElement->getKind()));
          return {};
        }
      }
      return value;
    } else {
      report_conversion_failure(node, std::addressof(rule), context,
                                missing_converter_message(rule.getName()));
      return {};
    }
  }

  template <typename Rule, typename Converter>
  static T convert_value(const Rule &rule, const CstNodeView &node,
                         const ValueBuildContext *context,
                         bool hasCustomValueConverter,
                         const Converter &value_converter) {
    if (!hasCustomValueConverter) {
      return convert_default_value(rule, node, context);
    }
    return finish_conversion<T>(value_converter(node, context), rule.getName(),
                                std::addressof(rule), node, context);
  }

  template <typename ConverterStorage, typename Converter>
  static void set_value_converter_from_option(ConverterStorage &storage,
                                              bool &hasCustomValueConverter,
                                              Converter &&converter) {
    using ConverterType = std::remove_cvref_t<Converter>;
    // A converter may return opt::ConversionResult<T> (fallible) or a value
    // convertible to T directly (infallible); as_conversion_result normalizes
    // both. The converter takes either the CstNodeView or its text.
    if constexpr (std::is_nothrow_invocable_v<ConverterType &,
                                              const CstNodeView &>) {
      storage = [converterFn = std::forward<Converter>(converter)](
                    const CstNodeView &node, const ValueBuildContext *)
                    mutable -> opt::ConversionResult<T> {
        return opt::as_conversion_result<T>(std::invoke(converterFn, node));
      };
      hasCustomValueConverter = true;
    } else if constexpr (std::is_nothrow_invocable_v<ConverterType &,
                                                     std::string_view>) {
      storage = [textConverter = normalize_text_converter<T>(
                     std::forward<Converter>(converter))](
                    const CstNodeView &node, const ValueBuildContext *)
                    mutable -> opt::ConversionResult<T> {
        return textConverter(node.getText());
      };
      hasCustomValueConverter = true;
    } else {
      static_assert(
          opt::detail::DependentFalse_v<ConverterType>,
          "DataTypeRule converter must be noexcept and invocable with "
          "const CstNodeView& or std::string_view.");
    }
  }
};

} // namespace pegium::parser::detail
