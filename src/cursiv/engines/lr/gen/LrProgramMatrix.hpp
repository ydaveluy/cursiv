// A fixed-width matrix whose rows share equal blocks of eight values.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <vector>

namespace cursiv {

struct LrProgramMatrix {
  std::vector<std::uint32_t> blocks;
  std::vector<std::uint64_t> values;

  void append(std::span<const std::uint64_t> row, std::uint64_t absent);
  unsigned index_bits() const;
  unsigned value_bits() const;
  // The narrowest of 8, 16, 32 and 64 bits that holds `max`.
  static unsigned bits(std::uint64_t max);

 private:
  std::map<std::array<std::uint64_t, 8>, std::uint32_t> _pool;
};

}  // namespace cursiv
