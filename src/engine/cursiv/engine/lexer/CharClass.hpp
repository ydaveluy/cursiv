// The scalars a character class reads, and the UTF-8 byte sequences that
// spell them.
#pragma once

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

#include "cursiv/text/unicode/UnicodeRange.hpp"

namespace cursiv::engine::gen {

struct ByteRange {
  std::uint8_t lo = 0;
  std::uint8_t hi = 0;
};

struct CharClass {
  // Ascending, disjoint, surrogates excluded.
  std::vector<unicode::Range> ranges;
  // Also reads one byte that spells no scalar.
  bool invalid = false;

  // The class of a `cpset` image node: its Unicode properties, its spelling
  // and whether it is complemented.
  static CharClass of(std::uint64_t props, bool complement, std::string_view spec);

  [[nodiscard]] bool has(char32_t scalar) const {
    return unicode::within(ranges.data(), ranges.size(), scalar);
  }
};

// Calls `emit(const ByteRange* seq, unsigned length)` for sequences of byte
// ranges that together spell exactly the scalars of `[first, last]`, in
// ascending order.
template <class Emit>
void utf8_sequences(char32_t first, char32_t last, Emit& emit) {
  const auto spell = [&](char32_t low, char32_t high, unsigned length, unsigned lead) {
    char32_t lo = std::max(first, low);
    const char32_t end = std::min(last, high);
    while (lo <= end) {
      char32_t hi = end;
      // After the first differing byte, every continuation spans its whole range.
      for (unsigned i = 1; i < length; ++i) {
        const char32_t mask = (char32_t{1} << (6 * i)) - 1;
        if ((lo & ~mask) == (hi & ~mask)) break;
        if ((lo & mask) != 0) {
          hi = lo | mask;
          break;
        }
        if ((hi & mask) != mask) hi = (hi & ~mask) - 1;
      }
      ByteRange seq[4];
      for (unsigned k = 0; k < length; ++k) {
        const unsigned shift = 6 * (length - 1 - k);
        const auto byte = [&](char32_t c) {
          return static_cast<std::uint8_t>(
              k == 0 ? (lead | (c >> shift)) : (0x80 | ((c >> shift) & 0x3F)));
        };
        seq[k] = {byte(lo), byte(hi)};
      }
      emit(seq, length);
      lo = hi + 1;
    }
  };
  spell(0, 0x7F, 1, 0);
  spell(0x80, 0x7FF, 2, 0xC0);
  spell(0x800, 0xD7FF, 3, 0xE0);
  spell(0xE000, 0xFFFF, 3, 0xE0);
  spell(0x10000, 0x10FFFF, 4, 0xF0);
}

}  // namespace cursiv::engine::gen
