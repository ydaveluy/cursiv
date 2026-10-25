// Two rows over one span, named as one: a creation folded into a shared rule
// travels, and the closing row above it states both halves as this pair.
#pragma once

#include <algorithm>
#include <bit>
#include <cstdint>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/core/utils/macros.hpp"

namespace cursiv {

struct LrFusedRow {
  // The creation the record holds.
  ElementId node = kNoElement;
  // The row that closes over it -- a directive, or a rule's own row.
  ElementId site = kNoElement;
};

// The index over the pairs is one open-addressed table keyed by both halves:
// a probe is one load on the latency-bound path. Its multiplier is searched
// per grammar, since which one scatters a table's keys is a fact about them.
inline constexpr std::uint32_t kLrNoFusedKey = 0xFFFFFFFFu;

constexpr std::uint32_t lr_fused_key(ElementId held, ElementId over) {
  return (static_cast<std::uint32_t>(held) << 16) | over;
}

// Only `kNoElement` on both halves packs to the empty marker, and a real
// pair's halves are grammar rows below it.
static_assert(kLrNoFusedKey == lr_fused_key(kNoElement, kNoElement),
              "the empty marker of a fused table is the pair of no elements");

// Twice the pairs, rounded up: at half load a probe reads one slot.
constexpr unsigned lr_fused_slots(unsigned nfused) {
  return std::max(8u, std::bit_ceil(2 * nfused));
}
constexpr unsigned lr_fused_shift(unsigned slots) {
  return 32u - static_cast<unsigned>(std::countr_zero(slots));
}

// Linear probing; the value is the row's own index, since a row's position
// is its element id.
constexpr void lr_fused_hash(const LrFusedRow* fused, unsigned nfused,
                             unsigned slots, std::uint32_t mul,
                             std::uint32_t* key, std::uint16_t* val) {
  const unsigned shift = lr_fused_shift(slots);
  const unsigned mask = slots - 1;
  for (unsigned i = 0; i < slots; ++i) {
    key[i] = kLrNoFusedKey;
    val[i] = 0;
  }
  for (unsigned j = 0; j < nfused; ++j) {
    const std::uint32_t k = lr_fused_key(fused[j].node, fused[j].site);
    unsigned h = (k * mul) >> shift;
    while (key[h] != kLrNoFusedKey) h = (h + 1) & mask;
    key[h] = k;
    val[h] = static_cast<std::uint16_t>(j);
  }
}

// Emptiness is tested first: `kNoElement` on both halves is the empty marker.
CURSIV_ALWAYS_INLINE constexpr ElementId lr_fused_probe(
    const std::uint32_t* key, const std::uint16_t* val, std::uint32_t mul,
    unsigned shift, unsigned mask, ElementId first, ElementId held,
    ElementId over) {
  const std::uint32_t k = lr_fused_key(held, over);
  unsigned h = (k * mul) >> shift;
  for (;;) {
    const std::uint32_t at = key[h];
    if (at == kLrNoFusedKey) return kNoElement;
    if (at == k) return static_cast<ElementId>(first + val[h]);
    h = (h + 1) & mask;
  }
}

}  // namespace cursiv
