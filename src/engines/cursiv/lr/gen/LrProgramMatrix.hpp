// A fixed-width matrix whose rows share equal blocks of eight values, and
// equal rows, as `LrProgramRows` reads it.
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
  // Per row appended, where its run of blocks starts: equal rows are stored
  // once.
  std::vector<std::uint32_t> rows;
  // Per row stored, its run of blocks.
  std::vector<std::uint32_t> blocks;
  // The distinct blocks.
  std::vector<std::uint64_t> values;

  void append(std::span<const std::uint64_t> row, std::uint64_t absent);
  // Frees what only `append` reads; no row is appended after.
  void finish();
  unsigned row_bits() const;
  unsigned index_bits() const;
  unsigned value_bits() const;
  // The narrowest of 8, 16, 32 and 64 bits that holds `max`.
  static unsigned bits(std::uint64_t max);

 private:
  std::map<std::array<std::uint64_t, 8>, std::uint32_t> _pool;
  // The block of nothing but its absent value, once stored: that value and its
  // index in `_pool`.
  std::optional<std::pair<std::uint64_t, std::uint32_t>> _empty;
  std::map<std::vector<std::uint32_t>, std::uint32_t> _runs;
};

}  // namespace cursiv::engine
