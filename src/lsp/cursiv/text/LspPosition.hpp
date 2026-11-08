#pragma once

#include <lsp/types.h>

#include <cstdint>

#include "cursiv/text/Position.hpp"
#include "cursiv/text/Range.hpp"

// `cursiv::Position` and `lsp::Position` are both a line and a column in
// UTF-16 code units, so the conversion is a field-for-field copy. The core
// may not include lsp-framework, so the unit crosses the frontier here and
// the type does not.

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
