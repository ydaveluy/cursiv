#pragma once

#include <lsp/types.h>

#include <cstdint>

#include "cursiv/text/Position.hpp"
#include "cursiv/text/Range.hpp"

// Both positions are a line and a UTF-16 column: a field copy, made here
// because the core may not include lsp-framework.

namespace cursiv {

[[nodiscard]] constexpr Position from_lsp(const lsp::Position& p) noexcept {
  return Position{static_cast<std::uint32_t>(p.line),
                  static_cast<std::uint32_t>(p.character)};
}

[[nodiscard]] constexpr Range from_lsp(const lsp::Range& r) noexcept {
  return Range{from_lsp(r.start), from_lsp(r.end)};
}

[[nodiscard]] constexpr lsp::Position to_lsp(Position p) noexcept {
  return lsp::Position{static_cast<lsp::Uint>(p.line),
                       static_cast<lsp::Uint>(p.character)};
}

[[nodiscard]] constexpr lsp::Range to_lsp(Range r) noexcept {
  return lsp::Range{to_lsp(r.start), to_lsp(r.end)};
}

}  // namespace cursiv
