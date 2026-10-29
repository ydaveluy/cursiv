#include "cursiv/engines/lr/gen/LrProgramMatrix.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>

#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"

namespace cursiv {

void LrProgramMatrix::append(std::span<const std::uint64_t> row,
                             std::uint64_t absent) {
  _columns = static_cast<unsigned>(row.size());
  std::vector<std::uint32_t> run;
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
    run.push_back(found->second);
  }
  const auto [laid, fresh] =
      _runs.try_emplace(run, static_cast<std::uint32_t>(blocks.size()));
  if (fresh) blocks.insert(blocks.end(), run.begin(), run.end());
  rows.push_back(laid->second);
}

void LrProgramMatrix::seal() {
  const std::size_t distinct = _runs.size();
  _pool = {};
  _runs = {};
  const std::size_t stride = (std::size_t{_columns} + 7) / 8;
  const std::size_t states = rows.size();
  const unsigned ib = index_bits(), vb = value_bits();
  // The bits of a form, by whether it shares rows and lays blocks.
  const auto size = [&](bool shared, bool blocked) {
    const std::size_t width = blocked ? stride : _columns;
    const std::size_t last = distinct == 0 ? 0 : (distinct - 1) * width;
    return (shared ? states * bits(last) : 0) +
           (shared ? distinct : states) * width * (blocked ? ib : vb) +
           (blocked ? values.size() * vb : 0);
  };
  bool shared = false, blocked = false;
  for (const bool s : {false, true})
    for (const bool b : {false, true})
      if (size(s, b) < size(shared, blocked)) shared = s, blocked = b;
  // Where the run of each row laid starts in `blocks`.
  const std::size_t lines = shared ? distinct : states;
  const auto run = [&](std::size_t i) {
    return shared ? i * stride : std::size_t{rows[i]};
  };
  if (!blocked) {
    std::vector<std::uint64_t> laid;
    laid.reserve(lines * _columns);
    for (std::size_t i = 0; i < lines; ++i)
      for (unsigned c = 0; c < _columns; ++c)
        laid.push_back(values[blocks[run(i) + c / 8] * 8 + c % 8]);
    values = std::move(laid);
    blocks = {};
    if (shared)
      for (std::uint32_t& r : rows)
        r = static_cast<std::uint32_t>(r / stride * _columns);
  } else if (!shared) {
    std::vector<std::uint32_t> laid;
    laid.reserve(lines * stride);
    for (std::size_t i = 0; i < lines; ++i)
      laid.insert(laid.end(), blocks.begin() + run(i),
                  blocks.begin() + run(i) + stride);
    blocks = std::move(laid);
  }
  if (!shared) rows = {};
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

}  // namespace cursiv
