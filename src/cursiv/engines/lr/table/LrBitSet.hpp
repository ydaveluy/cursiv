// A fixed-width set of terminals or speculation origins.
#pragma once

#include <algorithm>
#include <bit>
#include <cstdint>

namespace cursiv {

template <unsigned Bits>
struct LrBitSet {
  static constexpr unsigned kWords = (Bits + 63) / 64;
  std::uint64_t w[kWords]{};

  constexpr void set(unsigned i) { w[i >> 6] |= std::uint64_t{1} << (i & 63); }
  constexpr void reset(unsigned i) {
    w[i >> 6] &= ~(std::uint64_t{1} << (i & 63));
  }
  constexpr bool test(unsigned i) const {
    return ((w[i >> 6] >> (i & 63)) & 1) != 0;
  }
  // The index of `i` in a row laid in bit order.
  constexpr unsigned rank(unsigned i) const {
    unsigned r = 0;
    for (unsigned k = 0; k < (i >> 6); ++k)
      r += static_cast<unsigned>(std::popcount(w[k]));
    return r + static_cast<unsigned>(std::popcount(
                   w[i >> 6] & ((std::uint64_t{1} << (i & 63)) - 1)));
  }

  constexpr bool any() const {
    return std::any_of(w, w + kWords, [](std::uint64_t x) { return x != 0; });
  }
  constexpr bool none() const { return !any(); }

  constexpr LrBitSet& operator|=(const LrBitSet& o) {
    for (unsigned k = 0; k < kWords; ++k) w[k] |= o.w[k];
    return *this;
  }
  constexpr LrBitSet& operator&=(const LrBitSet& o) {
    for (unsigned k = 0; k < kWords; ++k) w[k] &= o.w[k];
    return *this;
  }
  constexpr LrBitSet& andnot(const LrBitSet& o) {
    for (unsigned k = 0; k < kWords; ++k) w[k] &= ~o.w[k];
    return *this;
  }

  friend constexpr bool operator==(const LrBitSet& a, const LrBitSet& b) {
    return std::equal(a.w, a.w + kWords, b.w);
  }

  constexpr unsigned first() const {
    for (unsigned k = 0; k < kWords; ++k)
      if (w[k] != 0)
        return (k << 6) | static_cast<unsigned>(std::countr_zero(w[k]));
    return Bits;
  }
  // Clears the lowest member and names it.
  constexpr unsigned take() {
    const unsigned i = first();
    if (i < Bits) reset(i);
    return i;
  }
};

}  // namespace cursiv
