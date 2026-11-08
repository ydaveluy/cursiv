// A range of code points, and the lookup over a sorted, disjoint row of them:
// the property tables are such rows, and so is a class's own past ASCII.
#pragma once

#include <cstddef>
#include <numeric>

namespace cursiv::unicode {

struct Range {
  char32_t lo;
  char32_t hi;
  friend constexpr bool operator==(const Range&, const Range&) = default;
};

constexpr bool within(const Range* r, std::size_t n, char32_t cp) {
  std::size_t lo = 0, hi = n;
  while (lo < hi) {
    const std::size_t mid = std::midpoint(lo, hi);
    if (r[mid].hi < cp)
      lo = mid + 1;
    else
      hi = mid;
  }
  return lo < n && r[lo].lo <= cp;
}

}  // namespace cursiv::unicode
