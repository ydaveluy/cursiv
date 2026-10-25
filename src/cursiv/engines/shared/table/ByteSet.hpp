// A byte set as four words: the form the construction interns and compares.
// `CharSet` is the grammar's spelling and has no equality by design, so the
// lowering converts once and everything downstream reads this one.
#pragma once

#include <bit>
#include <cstdint>

#include "cursiv/core/utils/ctstring.hpp"

namespace cursiv {

struct ByteSet {
  std::uint64_t w[4]{};

  constexpr void add(unsigned char b) {
    w[b >> 6] |= std::uint64_t{1} << (b & 63);
  }
  // The bytes from `lo` to `hi`, both included.
  constexpr void add(unsigned lo, unsigned hi) {
    for (unsigned b = lo; b <= hi; ++b) add(static_cast<unsigned char>(b));
  }
  constexpr bool has(unsigned char b) const {
    return ((w[b >> 6] >> (b & 63)) & 1) != 0;
  }
  constexpr bool any() const { return (w[0] | w[1] | w[2] | w[3]) != 0; }
  constexpr unsigned count() const {
    unsigned n = 0;
    for (const std::uint64_t x : w) n += static_cast<unsigned>(std::popcount(x));
    return n;
  }

  constexpr ByteSet& operator|=(const ByteSet& o) {
    for (unsigned k = 0; k < 4; ++k) w[k] |= o.w[k];
    return *this;
  }
  constexpr ByteSet& operator&=(const ByteSet& o) {
    for (unsigned k = 0; k < 4; ++k) w[k] &= o.w[k];
    return *this;
  }
  constexpr ByteSet& andnot(const ByteSet& o) {
    for (unsigned k = 0; k < 4; ++k) w[k] &= ~o.w[k];
    return *this;
  }
  friend constexpr ByteSet operator|(ByteSet a, const ByteSet& b) {
    return a |= b;
  }
  friend constexpr ByteSet operator&(ByteSet a, const ByteSet& b) {
    return a &= b;
  }
  constexpr bool intersects(const ByteSet& o) const { return (*this & o).any(); }
  // Every byte of `o` is here.
  constexpr bool covers(const ByteSet& o) const {
    return !ByteSet{o}.andnot(*this).any();
  }
  friend constexpr bool operator==(const ByteSet&, const ByteSet&) = default;
};

constexpr ByteSet bytes_of(const CharSet& cs) {
  ByteSet s;
  for (unsigned b = 0; b < 256; ++b)
    if (cs.test(static_cast<unsigned char>(b)))
      s.add(static_cast<unsigned char>(b));
  return s;
}

}  // namespace cursiv
