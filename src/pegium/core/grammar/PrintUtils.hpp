#pragma once

#include <cstddef>
#include <ostream>
#include <string_view>

#include <pegium/core/grammar/AbstractElement.hpp>
#include <pegium/core/grammar/AbstractRule.hpp>

namespace pegium::grammar::detail {

[[nodiscard]] constexpr bool is_rule_element_kind(ElementKind kind) noexcept {
  using enum ElementKind;
  switch (kind) {
  case DataTypeRule:
  case ParserRule:
  case TerminalRule:
  case InfixRule:
    return true;
  default:
    return false;
  }
}

inline std::ostream &print_element_reference(std::ostream &os,
                                             const AbstractElement &element) {
  if (is_rule_element_kind(element.getKind())) {
    return os << static_cast<const AbstractRule &>(element).getName();
  }
  return os << element;
}

/// Renders an n-ary element container (anything exposing `size()`/`get(index)`)
/// as `(e0<sep>e1<sep>...)`.
template <typename Nary>
std::ostream &print_nary(std::ostream &os, const Nary &nary,
                         std::string_view separator) {
  os << '(';
  const auto elementCount = nary.size();
  for (std::size_t elementIndex = 0; elementIndex < elementCount;
       ++elementIndex) {
    if (elementIndex > 0) {
      os << separator;
    }
    print_element_reference(os, *nary.get(elementIndex));
  }
  return os << ')';
}

/// Renders a rule (anything exposing `getName()`/`getTypeName()`/`getElement()`)
/// as `<keyword>Name returns Type: <element>;`. `keyword` carries its own
/// trailing space when present (e.g. `"terminal "`).
template <typename Rule>
std::ostream &print_rule(std::ostream &os, const Rule &rule,
                         std::string_view keyword = {}) {
  os << keyword << rule.getName() << " returns " << rule.getTypeName() << ": ";
  print_element_reference(os, *rule.getElement());
  return os << ';';
}

} // namespace pegium::grammar::detail
