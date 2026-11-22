#pragma once

#include <lsp/types.h>

#include <cstdint>

#include "cursiv/text/Position.hpp"
#include "cursiv/text/Range.hpp"

// Both positions are a line and a UTF-16 column: a field copy, made here
// because the core may not include lsp-framework.

namespace cursiv {

[[nodiscard]] constexpr Position fromLsp(const lsp::Position& p) noexcept {
  return Position{static_cast<std::uint32_t>(p.line),
                  static_cast<std::uint32_t>(p.character)};
}

[[nodiscard]] constexpr Range fromLsp(const lsp::Range& r) noexcept {
  return Range{fromLsp(r.start), fromLsp(r.end)};
}

[[nodiscard]] constexpr lsp::Position toLsp(Position p) noexcept {
  return lsp::Position{static_cast<lsp::Uint>(p.line),
                       static_cast<lsp::Uint>(p.character)};
}

[[nodiscard]] constexpr lsp::Range toLsp(Range r) noexcept {
  return lsp::Range{toLsp(r.start), toLsp(r.end)};
}

}  // namespace cursiv
