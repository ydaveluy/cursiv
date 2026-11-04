#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

// Case folding for name lookups: ASCII, eight bytes at a stroke. The scope
// walk and the index key fold with the same functions, or a query would hash
// into a bucket its comparison never accepts.

namespace cursiv {

namespace detail {

// One word, every 'A'..'Z' byte lowered, anything non-ASCII untouched.
[[nodiscard]] inline std::uint64_t fold_word(std::uint64_t w) noexcept {
  constexpr std::uint64_t kLow = 0x7f7f7f7f7f7f7f7full;
  constexpr std::uint64_t kHigh = 0x8080808080808080ull;
  const std::uint64_t low = w & kLow;
  const std::uint64_t geA = low + 0x3f3f3f3f3f3f3f3full;  // byte >= 'A'
  const std::uint64_t gtZ = low + 0x2525252525252525ull;  // byte >  'Z'
  const std::uint64_t upper = geA & ~gtZ & ~w & kHigh;
  return w | (upper >> 2u);  // 0x80 >> 2 == the case bit
}

template <class Word>
[[nodiscard]] inline std::uint64_t load(const char* p) noexcept {
  Word w = 0;
  std::memcpy(&w, p, sizeof w);  // constant size: one load, no call
  return w;
}

// The last, partial word, in pieces of four, two and one bytes: a
// variable-length memcpy is a call, paid once per short name -- which is
// most names.
[[nodiscard]] inline std::uint64_t load_tail(const char* p,
                                             std::size_t n) noexcept {
  std::uint64_t w = 0;
  std::size_t at = 0;
  if ((n & 4u) != 0) {
    w = load<std::uint32_t>(p);
    at = 4;
  }
  if ((n & 2u) != 0) {
    w |= load<std::uint16_t>(p + at) << (8u * at);
    at += 2;
  }
  if ((n & 1u) != 0) w |= load<std::uint8_t>(p + at) << (8u * at);
  return w;
}

// The last `n < 8` bytes of a name `whole` bytes long. A name a word long at
// least reads the word it ends with instead, overlapping bytes already read:
// one load where the pieces take three. Chosen by the length alone, so two
// names of one length always read the same way.
[[nodiscard]] inline std::uint64_t load_last(const char* p, std::size_t n,
                                             std::size_t whole) noexcept {
  return whole >= 8 ? load<std::uint64_t>(p + n - 8) : load_tail(p, n);
}

}  // namespace detail

// Multiply-xor over the folded words -- one pass, no temporary string. The
// length seeds the hash, so a short name and its zero-padded tail differ.
[[nodiscard]] inline std::uint64_t folded_hash(std::string_view name) noexcept {
  constexpr std::uint64_t kMix = 0x9e3779b97f4a7c15ull;
  std::uint64_t h = 0xcbf29ce484222325ull ^ (name.size() * kMix);
  const char* p = name.data();
  std::size_t n = name.size();
  for (; n >= 8; p += 8, n -= 8) {
    h ^= detail::fold_word(detail::load<std::uint64_t>(p));
    h *= kMix;
  }
  if (n != 0) {
    h ^= detail::fold_word(detail::load_last(p, n, name.size()));
    h *= kMix;
  }
  // Full avalanche, not a fold: the table indexes with the low bits and tags
  // with the high ones, so a short name's entropy must reach all the way up.
  h ^= h >> 33u;
  h *= 0xff51afd7ed558ccdull;
  h ^= h >> 33u;
  h *= 0xc4ceb9fe1a85ec53ull;
  return h ^ (h >> 33u);
}

[[nodiscard]] inline bool folded_equal(std::string_view a,
                                       std::string_view b) noexcept {
  if (a.size() != b.size()) return false;
  const char* pa = a.data();
  const char* pb = b.data();
  std::size_t n = a.size();
  for (; n >= 8; pa += 8, pb += 8, n -= 8)
    if (detail::fold_word(detail::load<std::uint64_t>(pa)) !=
        detail::fold_word(detail::load<std::uint64_t>(pb)))
      return false;
  return n == 0 || detail::fold_word(detail::load_tail(pa, n)) ==
                       detail::fold_word(detail::load_tail(pb, n));
}

}  // namespace cursiv
