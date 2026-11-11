// Two rows over one span, named as one: a creation folded into a shared rule
// travels, and the closing row above it states both halves as this pair.
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "cursiv/grammar/ElementId.hpp"
#include "cursiv/utils/macros.hpp"

namespace cursiv::engine {

struct LrFusedRow {
  // The creation the record holds.
  ElementId node = kNoElement;
  // The row that closes over it: a directive, or a rule's own row.
  ElementId site = kNoElement;
};

// The index over the pairs is a row displacement: the pair of `held` and
// `over` is the entry at `base[held] + over`, which names `held` beside the
// pair's index. A lookup is two loads and one check, with no probing.
inline constexpr std::uint32_t kLrNoFusedEntry = 0xFFFFFFFFu;

constexpr std::uint32_t lr_fused_key(ElementId held, ElementId over) {
  return (static_cast<std::uint32_t>(held) << 16) | over;
}

// The entries, and `base` for every `held` below the largest pair's, rows
// placed fullest first at the lowest offset where their cells are free.
inline std::vector<std::uint32_t> lr_fused_pack(
    const LrFusedRow* fused, unsigned nfused, std::vector<std::uint32_t>& base,
    unsigned& width) {
  std::vector<std::vector<unsigned>> rows;
  width = 0;
  for (unsigned j = 0; j < nfused; ++j) {
    if (fused[j].node >= rows.size()) rows.resize(fused[j].node + 1u);
    rows[fused[j].node].push_back(j);
    width = std::max(width, fused[j].site + 1u);
  }
  std::vector<unsigned> order;
  for (unsigned h = 0; h < rows.size(); ++h)
    if (!rows[h].empty()) order.push_back(h);
  std::ranges::stable_sort(order, [&](unsigned x, unsigned y) {
    return rows[x].size() > rows[y].size();
  });
  base.assign(rows.size(), 0);
  std::vector<std::uint32_t> entry;
  const auto free = [&](std::size_t at) {
    return at >= entry.size() || entry[at] == kLrNoFusedEntry;
  };
  for (const unsigned h : order) {
    std::uint32_t b = 0;
    while (!std::ranges::all_of(
        rows[h], [&](unsigned j) { return free(b + fused[j].site); }))
      ++b;
    for (const unsigned j : rows[h]) {
      if (b + fused[j].site >= entry.size())
        entry.resize(b + fused[j].site + 1u, kLrNoFusedEntry);
      entry[b + fused[j].site] = h << 16 | j;
    }
    base[h] = b;
  }
  entry.resize(*std::ranges::max_element(base) + width, kLrNoFusedEntry);
  return entry;
}

// A `held` or an `over` past the pairs' is a miss before any load.
template <class Offset>
CURSIV_ALWAYS_INLINE constexpr ElementId lr_fused_probe(
    const Offset* base, unsigned rows, const std::uint32_t* entry,
    unsigned width, ElementId first, ElementId held, ElementId over) {
  if (held >= rows || over >= width) return kNoElement;
  const std::uint32_t at = entry[base[held] + over];
  return at >> 16 == held ? static_cast<ElementId>(first + (at & 0xFFFFu))
                          : kNoElement;
}

}  // namespace cursiv::engine
