#include "cursiv/core/utils/EditDistance.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

namespace cursiv {

// Bit-parallel over the bytes of `a` (Hyyrö 2003), which the 64-byte bound
// fits in one word: one step per byte of `b`.
std::size_t edit_distance(std::string_view a, std::string_view b,
                          std::size_t limit) {
  constexpr std::size_t kWidest = 64;
  const std::size_t n = a.size();
  const std::size_t m = b.size();
  const std::size_t exceeded =
      limit == std::numeric_limits<std::size_t>::max() ? limit : limit + 1;
  if (n > kWidest || m > kWidest) return exceeded;
  if ((n > m ? n - m : m - n) > limit) return exceeded;
  if (n == 0) return std::min(m, exceeded);
  std::array<std::uint64_t, 256> match{};
  for (std::size_t i = 0; i < n; ++i)
    match[static_cast<unsigned char>(a[i])] |= std::uint64_t{1} << i;
  const std::uint64_t last = std::uint64_t{1} << (n - 1);
  std::uint64_t vp = ~std::uint64_t{0};
  std::uint64_t vn = 0;
  std::uint64_t d0 = 0;
  std::uint64_t before = 0;
  std::size_t distance = n;
  for (const char c : b) {
    const std::uint64_t eq = match[static_cast<unsigned char>(c)];
    const std::uint64_t swap = ((~d0 & eq) << 1) & before;
    d0 = (((eq & vp) + vp) ^ vp) | eq | vn | swap;
    std::uint64_t hp = vn | ~(d0 | vp);
    std::uint64_t hn = d0 & vp;
    distance += (hp & last) != 0;
    distance -= (hn & last) != 0;
    hp = (hp << 1) | 1;
    hn <<= 1;
    vp = hn | ~(d0 | hp);
    vn = hp & d0;
    before = eq;
  }
  return std::min(distance, exceeded);
}

}  // namespace cursiv
