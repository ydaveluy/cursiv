#include "cursiv/lr/gen/LrProgramMatrix.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>

#include "cursiv/shared/gen/LowerVerdicts.hpp"

namespace cursiv::engine {

void LrProgramMatrix::append(std::span<const std::uint64_t> row,
                             std::uint64_t absent) {
  std::vector<std::uint32_t> run;
  for (std::size_t at = 0; at < row.size(); at += 8) {
    std::array<std::uint64_t, 8> block;
    block.fill(absent);
    std::copy_n(row.begin() + at, std::min<std::size_t>(8, row.size() - at),
                block.begin());
    // Most blocks of a sparse row hold nothing: that block is looked up once.
    const bool empty = std::ranges::all_of(
        block, [&](std::uint64_t v) { return v == absent; });
    if (empty && _empty && _empty->first == absent) {
      run.push_back(_empty->second);
      continue;
    }
    auto found = _pool.find(block);
    if (found == _pool.end()) {
      if (values.size() / 8 > std::numeric_limits<std::uint32_t>::max())
        throw LowerRefused(LowerFail::tableTooLarge);
      found =
          _pool
              .try_emplace(block, static_cast<std::uint32_t>(values.size() / 8))
              .first;
      values.insert(values.end(), block.begin(), block.end());
    }
    if (empty) _empty.emplace(absent, found->second);
    run.push_back(found->second);
  }
  const auto [slot, fresh] =
      _runs.try_emplace(run, static_cast<std::uint32_t>(blocks.size()));
  if (fresh) blocks.insert(blocks.end(), run.begin(), run.end());
  rows.push_back(slot->second);
}

void LrProgramMatrix::finish() {
  _pool = {};
  _empty.reset();
  _runs = {};
}

unsigned LrProgramMatrix::bits(std::uint64_t max) {
  return max <= 0xffu ? 8 : max <= 0xffffu ? 16 : max <= 0xffffffffu ? 32 : 64;
}

unsigned LrProgramMatrix::row_bits() const {
  return rows.empty() ? 8 : bits(std::ranges::max(rows));
}

unsigned LrProgramMatrix::index_bits() const {
  return blocks.empty() ? 8 : bits(values.size() / 8 - 1);
}

unsigned LrProgramMatrix::value_bits() const {
  return values.empty() ? 8 : bits(std::ranges::max(values));
}

}  // namespace cursiv::engine
