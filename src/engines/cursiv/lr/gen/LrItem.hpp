// The item domain the construction indexes, and the flat pool of a state's
// rows.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cursiv::engine {

// An item is a dot position, indexed densely in (prod, dot) order so a packed
// word set enumerates already sorted. Its lookahead lives beside it.
struct LrItem {
  std::uint16_t prod = 0;
  std::uint16_t dot = 0;
  bool operator==(const LrItem&) const = default;
};

// A flat pool cut in rows, one per state. A row holds only until the pool
// grows.
template <class T>
struct LrPoolRows {
  std::vector<T> pool;
  std::vector<std::uint32_t> at, len;

  std::span<const T> operator[](std::size_t s) const {
    return {pool.data() + at[s], len[s]};
  }
  void append(std::size_t s, std::span<const T> row) {
    if (at.size() <= s) {
      at.resize(s + 1);
      len.resize(s + 1);
    }
    at[s] = static_cast<std::uint32_t>(pool.size());
    len[s] = static_cast<std::uint32_t>(row.size());
    pool.insert(pool.end(), row.begin(), row.end());
  }
};

}  // namespace cursiv::engine
