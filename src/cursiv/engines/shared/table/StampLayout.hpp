// How an engine splits a record's opaque bits: the row low, above it the state
// that read the record. "No row" is the row field's full mask.
#pragma once

#include <bit>
#include <cstdint>
#include <limits>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"

namespace cursiv {

struct StampLayout {
  unsigned rowBits = std::numeric_limits<ElementId>::digits;

  static constexpr unsigned record_bits() {
    CstNode n{};
    n.engine = n.engine - 1u;
    return static_cast<unsigned>(std::bit_width(std::uint32_t{n.engine}));
  }
  constexpr unsigned state_bits() const { return record_bits() - rowBits; }
  constexpr std::uint32_t row_mask() const {
    return (std::uint32_t{1} << rowBits) - 1u;
  }

  constexpr std::uint32_t make(ElementId row, std::uint16_t state) const {
    return (row & row_mask()) | (std::uint32_t{state} << rowBits);
  }
  constexpr ElementId row(const CstNode& n) const {
    const std::uint32_t r = n.engine & row_mask();
    return r == row_mask() ? kNoElement : static_cast<ElementId>(r);
  }
  constexpr std::uint16_t state(const CstNode& n) const {
    return static_cast<std::uint16_t>(n.engine >> rowBits);
  }

  // The row of an already written record, its state left alone.
  constexpr void set_row(CstNode& n, ElementId row) const {
    n.engine = (n.engine & ~row_mask()) | (row & row_mask());
  }
  // The state of an already written record, its row left alone.
  constexpr void set_state(CstNode& n, std::uint16_t state) const {
    n.engine = (n.engine & row_mask()) | (std::uint32_t{state} << rowBits);
  }

  constexpr bool operator==(const StampLayout&) const = default;
};

}  // namespace cursiv
