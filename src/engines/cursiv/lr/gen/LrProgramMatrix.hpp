// A fixed-width matrix whose rows share equal blocks of eight values, and
// equal rows, stored in the smallest form `LrProgramRows` reads.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace cursiv::engine {

struct LrProgramMatrix {
  // Per row appended, where its row starts: equal rows are stored once. None
  // once finished where each row is stored for itself.
  std::vector<std::uint32_t> rows;
  // Per row stored, its run of blocks; none once finished where a row is stored
  // as its values.
  std::vector<std::uint32_t> blocks;
  // The distinct blocks, or the rows' values where no block is stored.
  std::vector<std::uint64_t> values;

  void append(std::span<const std::uint64_t> row, std::uint64_t absent);
  // Picks the smallest form once the last row is in, and frees what only
  // `append` reads; no row is appended after.
  void finish();
  unsigned row_bits() const;
  unsigned index_bits() const;
  unsigned value_bits() const;
  // The narrowest of 8, 16, 32 and 64 bits that holds `max`.
  static unsigned bits(std::uint64_t max);

 private:
  unsigned _columns = 0;
  std::map<std::array<std::uint64_t, 8>, std::uint32_t> _pool;
  // The block of nothing but its absent value, once stored: that value and its
  // index in `_pool`.
  std::optional<std::pair<std::uint64_t, std::uint32_t>> _empty;
  std::map<std::vector<std::uint32_t>, std::uint32_t> _runs;
};

}  // namespace cursiv::engine
