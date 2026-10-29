// A fixed-width matrix whose rows share equal blocks of eight values, and
// equal rows, laid in the smallest form `LrProgramRows` reads.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <vector>

namespace cursiv {

struct LrProgramMatrix {
  // Per row appended, where its row starts: equal rows are laid once. None
  // once sealed where each row is laid for itself.
  std::vector<std::uint32_t> rows;
  // Per row laid, its run of blocks; none once sealed where a row is laid as
  // its values.
  std::vector<std::uint32_t> blocks;
  // The distinct blocks, or the rows' values where no block is laid.
  std::vector<std::uint64_t> values;

  void append(std::span<const std::uint64_t> row, std::uint64_t absent);
  // Lays the smallest form once the last row is in, and frees what only
  // `append` reads: a row appended after shares nothing with the earlier ones.
  void seal();
  unsigned row_bits() const;
  unsigned index_bits() const;
  unsigned value_bits() const;
  // The narrowest of 8, 16, 32 and 64 bits that holds `max`.
  static unsigned bits(std::uint64_t max);

 private:
  unsigned _columns = 0;
  std::map<std::array<std::uint64_t, 8>, std::uint32_t> _pool;
  std::map<std::vector<std::uint32_t>, std::uint32_t> _runs;
};

}  // namespace cursiv
