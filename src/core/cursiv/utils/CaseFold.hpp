#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string_view>

#include "cursiv/grammar/unicode/UnicodeFold.hpp"
#include "cursiv/text/scalar_at.hpp"
#include "cursiv/utils/macros.hpp"

// Case folding for case-insensitive keywords and names: Unicode simple
// folding between cases of the same UTF-8 length. The scope walk and the index
// key must fold with these same functions.

namespace cursiv {

namespace detail {

// The scalar `cp` folds to: its lower case, mostly.
[[nodiscard]] constexpr char32_t fold_scalar(char32_t cp) noexcept {
  if (cp >= unicode::detail::kFoldEnd) return cp;
  const std::uint8_t block = unicode::detail::kFoldPage[cp >> 6u];
  return static_cast<char32_t>(
      static_cast<std::int32_t>(cp) +
      unicode::detail::kFoldDelta[unicode::detail::kFoldBlock[block]
                                                             [cp & 63u]]);
}

// Calls `each` with every other scalar that folds as `cp` does -- its other
// cases -- the one it folds to first.
template <class Each>
constexpr void for_each_other_case(char32_t cp, Each&& each) {
  if (cp < 0x80) {  // an ASCII letter has its other ASCII case alone
    if (cp - U'a' < 26u) each(cp - 32);
    if (cp - U'A' < 26u) each(cp + 32);
    return;
  }
  const char32_t to = fold_scalar(cp);
  if (to != cp) each(to);
  for (std::size_t page = 2; page < std::size(unicode::detail::kFoldPage);
       ++page) {
    const std::uint8_t block = unicode::detail::kFoldPage[page];
    if (block == 0) continue;
    for (std::size_t i = 0; i < 64; ++i) {
      const auto x = static_cast<char32_t>(page * 64 + i);
      if (unicode::detail::kFoldBlock[block][i] != 0 && x != cp &&
          fold_scalar(x) == to)
        each(x);
    }
  }
}

// Whether `cp` has another case.
[[nodiscard]] constexpr bool has_other_case(char32_t cp) {
  bool found = false;
  if (fold_scalar(cp) != cp) return true;
  for_each_other_case(cp, [&found](char32_t) { found = true; });
  return found;
}

// Whether `text` holds a letter with another case, which folding changes.
[[nodiscard]] constexpr bool has_cased_letter(std::string_view text) {
  for (std::size_t i = 0; i < text.size();) {
    std::size_t len = 0;
    const char32_t cp = scalar_at(text.data() + i, text.size() - i, len);
    if (len != 0 && has_other_case(cp)) return true;
    i += len != 0 ? len : 1;
  }
  return false;
}

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

// The last `n < 8` bytes of a name `whole` bytes long. A name a word long
// or more reads its last word instead, overlapping bytes already read.
[[nodiscard]] inline std::uint64_t load_last(const char* p, std::size_t n,
                                             std::size_t whole) noexcept {
  return whole >= 8 ? load<std::uint64_t>(p + n - 8) : load_tail(p, n);
}

inline constexpr std::uint64_t kHashMix = 0x9e3779b97f4a7c15ull;

// Full avalanche, not a fold: the table indexes with the low bits and tags
// with the high ones, so a short name's entropy must reach all the way up.
[[nodiscard]] inline std::uint64_t hash_avalanche(std::uint64_t h) noexcept {
  h ^= h >> 33u;
  h *= 0xff51afd7ed558ccdull;
  h ^= h >> 33u;
  h *= 0xc4ceb9fe1a85ec53ull;
  return h ^ (h >> 33u);
}

// The scalar at `p`, folded, and in `len` the bytes it takes. A byte that
// starts no scalar reads alone, as a value past every scalar.
[[nodiscard]] inline char32_t folded_at(const char* p, std::size_t avail,
                                        std::size_t& len) noexcept {
  const char32_t lead = static_cast<unsigned char>(*p);
  if (lead < 0x80u) {
    len = 1;
    return lead - U'A' < 26u ? lead + 32u : lead;
  }
  // Two bytes, as Latin, Greek and Cyrillic letters are, decoded in place.
  if (lead >= 0xC2u && lead < 0xE0u && avail >= 2 &&
      (static_cast<unsigned char>(p[1]) & 0xC0u) == 0x80u) {
    len = 2;
    return fold_scalar(((lead & 0x1Fu) << 6u) |
                       (static_cast<unsigned char>(p[1]) & 0x3Fu));
  }
  const char32_t cp = scalar_at(p, avail, len);
  if (len != 0) return fold_scalar(cp);
  len = 1;
  return 0x110000u + static_cast<unsigned char>(*p);
}

// A name holding a non-ASCII byte, which no ASCII name equals: multiply-xor
// over its folded scalars. Out of line, as is the compare below: inlined,
// they cost the ASCII path its registers.
[[nodiscard]] CURSIV_NOINLINE inline std::uint64_t folded_scalars_hash(
    const char* name, std::size_t size) noexcept {
  std::uint64_t h = 0x84222325cbf29ce4ull;
  for (std::size_t i = 0, len = 0; i < size; i += len)
    h = (h ^ folded_at(name + i, size - i, len)) * kHashMix;
  return hash_avalanche(h);
}

// Two names of `size` bytes, scalar by scalar, each folded: a letter and its
// other cases take as many bytes, so equal names stay in step.
[[nodiscard]] CURSIV_NOINLINE inline bool folded_scalars_equal(
    const char* a, const char* b, std::size_t size) noexcept {
  for (std::size_t i = 0, la = 0, lb = 0; i < size; i += la)
    if (folded_at(a + i, size - i, la) != folded_at(b + i, size - i, lb))
      return false;
  return true;
}

}  // namespace detail

// Multiply-xor over the folded words -- one pass, no temporary string. The
// length seeds the hash, so a short name and its zero-padded tail differ. A
// name with a non-ASCII byte hashes its folded scalars instead.
[[nodiscard]] inline std::uint64_t folded_hash(std::string_view name) noexcept {
  using detail::kHashMix;
  std::uint64_t h = 0xcbf29ce484222325ull ^ (name.size() * kHashMix);
  std::uint64_t bytes = 0;
  const char* p = name.data();
  std::size_t n = name.size();
  for (; n >= 8; p += 8, n -= 8) {
    const std::uint64_t w = detail::load<std::uint64_t>(p);
    bytes |= w;
    h ^= detail::fold_word(w);
    h *= kHashMix;
  }
  if (n != 0) {
    const std::uint64_t w = detail::load_last(p, n, name.size());
    bytes |= w;
    h ^= detail::fold_word(w);
    h *= kHashMix;
  }
  if ((bytes & 0x8080808080808080ull) != 0) [[unlikely]]
    return detail::folded_scalars_hash(name.data(), name.size());
  return detail::hash_avalanche(h);
}

namespace detail {

// How two names compare: `Folded` when only case tells them apart.
enum class NameMatch : unsigned { None, Folded, Exact };

// A word at a time, which is what nearly every lookup compares: names of
// different sizes never match, and a word that differs matters only where it
// holds a byte past ASCII.
[[nodiscard]] inline NameMatch name_match(std::string_view a,
                                          std::string_view b) noexcept {
  using enum NameMatch;
  if (a.size() != b.size()) return None;
  constexpr std::uint64_t kHigh = 0x8080808080808080ull;
  const char* pa = a.data();
  const char* pb = b.data();
  std::size_t n = a.size();
  // Past ASCII, the whole names are compared a scalar at a time.
  const auto scalars = [&](std::uint64_t wa, std::uint64_t wb) {
    return ((wa | wb) & kHigh) != 0 &&
                   folded_scalars_equal(a.data(), b.data(), a.size())
               ? Folded
               : None;
  };
  NameMatch match = Exact;
  for (; n >= 8; pa += 8, pb += 8, n -= 8) {
    const std::uint64_t wa = load<std::uint64_t>(pa);
    const std::uint64_t wb = load<std::uint64_t>(pb);
    if (wa == wb) continue;
    if (fold_word(wa) != fold_word(wb)) return scalars(wa, wb);
    match = Folded;
  }
  if (n == 0) return match;
  const std::uint64_t wa = load_last(pa, n, a.size());
  const std::uint64_t wb = load_last(pb, n, a.size());
  if (wa == wb) return match;
  return fold_word(wa) == fold_word(wb) ? Folded : scalars(wa, wb);
}

}  // namespace detail

// Whether two names are the same up to case.
[[nodiscard]] inline bool folded_equal(std::string_view a,
                                       std::string_view b) noexcept {
  return detail::name_match(a, b) != detail::NameMatch::None;
}

}  // namespace cursiv
