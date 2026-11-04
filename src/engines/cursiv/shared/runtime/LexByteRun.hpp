// The byte runs a lexer crosses, for every engine: a set's membership as one
// load, and eight bytes a step where the set has the shape for it.
#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>

#include "cursiv/utils/macros.hpp"

namespace cursiv {

// A set's bytes as a table built at compile time: a member is one load.
// The set is taken by value, so equal sets share one table.
template <std::array<std::uint64_t, 4> S>
inline constexpr auto kInSet = [] {
  std::array<bool, 256> t{};
  for (unsigned x = 0; x < 256; ++x)
    t[x] = ((S[x >> 6] >> (x & 63u)) & 1u) != 0;
  return t;
}();
template <std::array<std::uint64_t, 4> S>
CURSIV_ALWAYS_INLINE inline bool in_set(unsigned char b) {
  return kInSet<S>[b];
}

// Whether a table of four words per mode holds `b` for `mode`.
CURSIV_ALWAYS_INLINE inline bool mode_row_has(const std::uint64_t* rows,
                                              std::uint16_t mode,
                                              unsigned char b) {
  return ((rows[std::size_t{mode} * 4 + (b >> 6)] >> (b & 63u)) & 1u) != 0;
}

// Past the run of blanks from `p` that a reader crosses blind: on its last byte
// where that may read on with the byte behind it, for the walk to go on from.
template <class Skips, class Stops>
CURSIV_ALWAYS_INLINE inline std::uint32_t lex_past_run(const char* src,
                                                       std::uint32_t p,
                                                       std::uint32_t end,
                                                       Skips&& skips,
                                                       Stops&& stops) {
  const std::uint32_t from = p;
  while (p < end && skips(static_cast<unsigned char>(src[p]))) ++p;
  if (p != from && p < end && stops(static_cast<unsigned char>(src[p]))) --p;
  return p;
}

inline constexpr std::uint64_t kRep = 0x0101010101010101ull;
inline constexpr std::uint64_t kHi = 0x8080808080808080ull;
CURSIV_ALWAYS_INLINE inline std::uint64_t ld64(const char* p) {
  std::uint64_t w;
  std::memcpy(&w, p, 8);
  if constexpr (std::endian::native == std::endian::big) {
    w = ((w & 0x00ff00ff00ff00ffull) << 8) | ((w >> 8) & 0x00ff00ff00ff00ffull);
    w = ((w & 0x0000ffff0000ffffull) << 16) |
        ((w >> 16) & 0x0000ffff0000ffffull);
    w = (w << 32) | (w >> 32);
  }
  return w;
}

// `eq8` marks a byte equal to `b`, `lt8` a byte below it. ONLY THE LOWEST SET
// LANE IS EXACT: the borrow out of it can flag higher lanes that answer
// nothing, so a mask is read by `ctzll` alone -- a union of them keeps that,
// a negation or an intersection does not.
CURSIV_ALWAYS_INLINE inline std::uint64_t eq8(std::uint64_t w,
                                              unsigned char b) {
  const std::uint64_t x = w ^ (kRep * b);
  return (x - kRep) & ~x & kHi;
}

// ...and `lt8` answers no `b` above 0x80: `kRep * b` would set every lane's
// high bit and the subtraction borrow out of the lowest one, which is the
// lane read.
CURSIV_ALWAYS_INLINE inline std::uint64_t lt8(std::uint64_t w,
                                              unsigned char b) {
  return (w - kRep * b) & ~w & kHi;
}

// The high bit of every lane holding lo <= x <= hi, exact: the lanes are
// seven bits wide, so neither sum carries into its neighbour; the first sum
// reaches it from `lo` on, the second past `hi`, so they differ inside alone.
CURSIV_ALWAYS_INLINE inline std::uint64_t rng8(std::uint64_t w, unsigned lo,
                                               unsigned hi,
                                               unsigned mask = 0x7f) {
  const std::uint64_t x = w & (kRep * mask);
  return ((x + kRep * (0x80u - lo)) ^ (x + kRep * (0x7fu - hi))) & kHi;
}

}  // namespace cursiv
