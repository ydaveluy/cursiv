#pragma once

/// Grammar contract for terminal rules.

#include <concepts>
#include <pegium/core/grammar/AbstractRule.hpp>
#include <pegium/core/grammar/RuleValue.hpp>
#include <string>
#include <type_traits>

namespace pegium {
class CstNodeView;
}

namespace pegium::parser {
struct ValueBuildContext;
}

namespace pegium::grammar {

struct TerminalRule : AbstractRule {
  constexpr TerminalRule() noexcept : AbstractRule(ElementKind::TerminalRule) {}
  virtual RuleValue
  getValue(const CstNodeView &node,
           const parser::ValueBuildContext *context = nullptr) const = 0;
  virtual void
  appendTextValue(std::string &out, const CstNodeView &node,
                  const parser::ValueBuildContext *context = nullptr) const = 0;
  virtual const AbstractElement *getElement() const noexcept = 0;

  void print(std::ostream &os) const override;
};
template <typename T>
concept IsTerminalRule =
    std::derived_from<std::remove_cvref_t<T>, TerminalRule>;
    
} // namespace pegium::grammar
