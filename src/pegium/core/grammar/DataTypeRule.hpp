#pragma once

/// Grammar contract for rules producing scalar values.

#include <concepts>
#include <pegium/core/grammar/AbstractRule.hpp>
#include <pegium/core/grammar/RuleValue.hpp>
#include <type_traits>

namespace pegium {
class CstNodeView;
}

namespace pegium::parser {
struct ValueBuildContext;
}

namespace pegium::grammar {

struct DataTypeRule : AbstractRule {
  constexpr DataTypeRule() noexcept : AbstractRule(ElementKind::DataTypeRule) {}
  virtual RuleValue
  getValue(const CstNodeView &node,
           const parser::ValueBuildContext *context = nullptr) const = 0;
  virtual const AbstractElement *getElement() const noexcept = 0;

  void print(std::ostream &os) const override;
};
template <typename T>
concept IsDataTypeRule =
    std::derived_from<std::remove_cvref_t<T>, DataTypeRule>;
} // namespace pegium::grammar
