// A set of byte values.
#pragma once

#include <array>
#include <compare>
#include <cstdint>

namespace cursiv::engine::gen {

struct ByteSet {
  std::array<std::uint64_t, 4> words{};

  constexpr void add(unsigned b) { words[b >> 6] |= std::uint64_t{1} << (b & 63); }
  [[nodiscard]] constexpr bool has(unsigned b) const {
    return ((words[b >> 6] >> (b & 63)) & 1) != 0;
  }
  [[nodiscard]] constexpr bool any() const {
    return (words[0] | words[1] | words[2] | words[3]) != 0;
  }
  constexpr ByteSet& operator|=(const ByteSet& o) {
    for (unsigned i = 0; i < 4; ++i) words[i] |= o.words[i];
    return *this;
  }
  [[nodiscard]] constexpr ByteSet operator~() const {
    ByteSet out;
    for (unsigned i = 0; i < 4; ++i) out.words[i] = ~words[i];
    return out;
  }
  friend constexpr bool operator==(const ByteSet&, const ByteSet&) = default;
  friend constexpr auto operator<=>(const ByteSet&, const ByteSet&) = default;

  [[nodiscard]] static constexpr ByteSet of(unsigned b) {
    ByteSet out;
    out.add(b);
    return out;
  }
  [[nodiscard]] static constexpr ByteSet range(unsigned lo, unsigned hi) {
    ByteSet out;
    for (unsigned b = lo; b <= hi; ++b) out.add(b);
    return out;
  }
};

}  // namespace cursiv::engine::gen
