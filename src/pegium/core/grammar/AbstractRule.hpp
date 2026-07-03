#pragma once

/// Base grammar rule contract exposing rule names and produced type names.

#include <pegium/core/grammar/AbstractElement.hpp>
#include <string_view>
#include <ostream>
namespace pegium::grammar {

struct AbstractRule : AbstractElement {
  constexpr explicit AbstractRule(ElementKind kind) noexcept
      : AbstractElement(kind) {}
  constexpr ~AbstractRule() noexcept override = default;
  virtual std::string_view getTypeName() const noexcept = 0;
  virtual std::string_view getName() const noexcept = 0;
};

} // namespace pegium::grammar
