// The runs of a row ordered on the terminal.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace cursiv {

// The run `t` owns in a row of `n` cells, as an offset and a length.
template <class Cell>
std::pair<unsigned, unsigned> lr_term_run(const Cell* row, unsigned n,
                                          std::uint16_t t) {
  const std::span cells(row, n);
  const auto run = std::ranges::equal_range(cells, t, {}, &Cell::term);
  return {static_cast<unsigned>(run.begin() - cells.begin()),
          static_cast<unsigned>(run.size())};
}

// `f(i, j)` for each run `[i, j)` of a row of `n` cells, in row order. The
// body may reorder a run: its bounds are taken before it is called.
template <class Cell, class F>
void lr_each_run(const Cell* row, std::size_t n, F&& f) {
  for (std::size_t i = 0, j = 0; i < n; i = j) {
    j = i + 1;
    while (j < n && row[j].term == row[i].term) ++j;
    f(i, j);
  }
}

}  // namespace cursiv
