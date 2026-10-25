// A frozen matrix of shared blocks, indexed by state and symbol.
#pragma once

#include <cstddef>
#include <span>

namespace cursiv {

template <class Index, class Value>
struct LrProgramRows {
  std::span<const Index> blocks;
  std::span<const Value> values;
  unsigned columns = 0;
  Value absent = 0;

  constexpr Value get(unsigned state, unsigned symbol) const {
    if (symbol >= columns) return absent;
    const std::size_t stride = (std::size_t{columns} + 7) / 8;
    const std::size_t block = blocks[std::size_t{state} * stride + symbol / 8];
    return values[block * 8 + symbol % 8];
  }
};

}  // namespace cursiv
