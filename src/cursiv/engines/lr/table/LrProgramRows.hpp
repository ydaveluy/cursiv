// A frozen matrix indexed by state and symbol, in the smallest of four forms:
// a state's row is laid for it, or `rows` names where its shared row starts;
// a row is its values, or a run of shared blocks of eight values where
// `blocks` is laid. An empty span is a layer the matrix does not lay.
#pragma once

#include <cstddef>
#include <span>

namespace cursiv {

template <class Row, class Index, class Value>
struct LrProgramRows {
  std::span<const Row> rows;
  std::span<const Index> blocks;
  std::span<const Value> values;
  unsigned columns = 0;
  Value absent = 0;

  constexpr Value get(unsigned state, unsigned symbol) const {
    if (symbol >= columns) return absent;
    const std::size_t width = blocks.empty() ? columns : (columns + 7) / 8;
    const std::size_t at = rows.empty() ? state * width : rows[state];
    if (blocks.empty()) return values[at + symbol];
    return values[std::size_t{blocks[at + symbol / 8]} * 8 + symbol % 8];
  }
};

}  // namespace cursiv
