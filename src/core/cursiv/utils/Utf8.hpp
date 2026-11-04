#pragma once

#include <cstddef>

// How wide a UTF-8 sequence its lead byte announces. One rule, one place: what
// advances a cursor and what counts a column must agree.

namespace cursiv {

// The lead's own three bits of length. Anything that leads nothing -- a
// continuation byte, 0xF8 and above -- is one byte, which is the only answer
// that always moves.
//
// Masks, not a table: the width sits on the advance's dependency chain, where
// an L1 load costs more latency than three compares on a register already
// loaded.
[[nodiscard]] constexpr std::size_t utf8_width(char lead) noexcept {
  const auto b = static_cast<unsigned char>(lead);
  return (b & 0xF8u) == 0xF0u   ? 4
         : (b & 0xF0u) == 0xE0u ? 3
         : (b & 0xE0u) == 0xC0u ? 2
                                : 1;
}

}  // namespace cursiv
