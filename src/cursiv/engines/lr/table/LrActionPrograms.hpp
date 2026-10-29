// The action cells of a frozen table: a packet of ops named by its first
// index and its count in one word, cut at `countBits`.
#pragma once

#include <cstdint>
#include <span>

#include "cursiv/engines/lr/table/LrProgramRows.hpp"

namespace cursiv {

template <class Row, class Index, class Cell, class Op>
struct LrActionPrograms {
  LrProgramRows<Row, Index, Cell> rows;
  std::span<const Op> op;
  unsigned countBits = 0;

  constexpr Cell cell(unsigned state, unsigned term) const {
    return rows.get(state, term);
  }
  constexpr std::uint32_t first(Cell cell) const {
    return static_cast<std::uint32_t>(cell >> countBits);
  }
  constexpr unsigned count(Cell cell) const {
    return static_cast<unsigned>(cell & ((Cell{1} << countBits) - 1));
  }
};

}  // namespace cursiv
