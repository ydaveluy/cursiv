#include "cursiv/engines/lr/gen/LrProgramMatrix.hpp"

#include <algorithm>
#include <limits>

#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"

namespace cursiv {

void LrProgramMatrix::append(std::span<const std::uint64_t> row,
                             std::uint64_t absent) {
  for (std::size_t at = 0; at < row.size(); at += 8) {
    std::array<std::uint64_t, 8> block;
    block.fill(absent);
    std::copy_n(row.begin() + at, std::min<std::size_t>(8, row.size() - at),
                block.begin());
    auto found = _pool.find(block);
    if (found == _pool.end()) {
      if (values.size() / 8 > std::numeric_limits<std::uint32_t>::max())
        throw LowerRefused(LowerFail::tableTooLarge);
      found =
          _pool.emplace(block, static_cast<std::uint32_t>(values.size() / 8))
              .first;
      values.insert(values.end(), block.begin(), block.end());
    }
    blocks.push_back(found->second);
  }
}

unsigned LrProgramMatrix::bits(std::uint64_t max) {
  return max <= 0xffu ? 8 : max <= 0xffffu ? 16 : max <= 0xffffffffu ? 32 : 64;
}

unsigned LrProgramMatrix::index_bits() const {
  const auto count = values.size() / 8;
  return count == 0 ? 8 : bits(count - 1);
}

unsigned LrProgramMatrix::value_bits() const {
  return values.empty() ? 8 : bits(std::ranges::max(values));
}

}  // namespace cursiv
