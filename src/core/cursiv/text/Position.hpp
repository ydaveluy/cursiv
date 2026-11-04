#pragma once

#include <compare>
#include <cstdint>

// A position is where an editor points: a line and a column, not the byte
// offset a document stores. The unit travels, the type does not.

namespace cursiv {

// Zero-based, as LSP spells it: `character` counts UTF-16 code units, not
// code points, so a four-byte emoji counts as two without splitting the pair.
struct Position {
  std::uint32_t line = 0;
  std::uint32_t character = 0;

  [[nodiscard]] constexpr bool operator==(const Position&) const noexcept =
      default;
  // Document order, which the defaulted comparison already is: `line` is
  // declared first, so it wins and the column breaks the tie.
  [[nodiscard]] constexpr std::strong_ordering operator<=>(
      const Position&) const noexcept = default;
};

}  // namespace cursiv
