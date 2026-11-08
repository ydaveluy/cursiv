#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

// How wide a UTF-8 sequence its lead byte announces. One rule, one place: what
// advances a cursor and what counts a column must agree.

namespace cursiv::detail {

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

// One step of a decode that replaces, as an editor opening the file does: a
// well-formed sequence is its scalar, two units past the BMP, and each
// maximal ill-formed subpart is one U+FFFD.
struct Utf8Step {
  std::uint32_t units;
  std::uint32_t bytes;
  bool wellFormed;
};

[[nodiscard]] inline Utf8Step utf8_step(const char* p,
                                        std::size_t left) noexcept {
  const auto lead = static_cast<unsigned char>(p[0]);
  if (lead < 0x80U) return {1, 1, true};
  std::uint32_t need = 0;
  unsigned lo = 0x80U;
  unsigned hi = 0xBFU;
  if (lead >= 0xC2U && lead <= 0xDFU) {
    need = 1;
  } else if (lead >= 0xE0U && lead <= 0xEFU) {
    need = 2;
    if (lead == 0xE0U) lo = 0xA0U;
    if (lead == 0xEDU) hi = 0x9FU;
  } else if (lead >= 0xF0U && lead <= 0xF4U) {
    need = 3;
    if (lead == 0xF0U) lo = 0x90U;
    if (lead == 0xF4U) hi = 0x8FU;
  } else {
    return {1, 1, false};
  }
  for (std::uint32_t i = 1; i <= need; ++i) {
    if (i >= left) return {1, i, false};
    if (const auto next = static_cast<unsigned char>(p[i]);
        next < lo || next > hi)
      return {1, i, false};
    lo = 0x80U;
    hi = 0xBFU;
  }
  return {need == 3 ? 2U : 1U, need + 1, true};
}

[[nodiscard]] inline bool utf8_well_formed(std::string_view text) noexcept {
  for (std::size_t at = 0; at < text.size();) {
    const Utf8Step step = utf8_step(text.data() + at, text.size() - at);
    if (!step.wellFormed) return false;
    at += step.bytes;
  }
  return true;
}

}  // namespace cursiv::detail
