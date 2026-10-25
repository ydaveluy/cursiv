// The item domain the construction indexes, and the flat pool of a state's
// rows.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace cursiv {

// An item is a dot position, indexed densely in (prod, dot) order so a packed
// word set enumerates already sorted. Its lookahead lives beside it.
struct LrItem {
  std::uint16_t prod = 0;
  std::uint16_t dot = 0;
  bool operator==(const LrItem&) const = default;
};

// The rows of a flat pool, one per state.
template <class T>
struct LrPoolRows {
  const T* items = nullptr;
  const std::uint32_t* at = nullptr;
  const std::uint32_t* len = nullptr;

  std::span<const T> operator[](std::size_t s) const {
    return {items + at[s], len[s]};
  }
};

}  // namespace cursiv
