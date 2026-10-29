// The split of a record's stamp: the row keeps `ElementId`'s width while the
// states leave it room. The row field holds every row and the sentinel.
#pragma once

#include <algorithm>
#include <bit>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

#include "cursiv/engines/shared/table/StampLayout.hpp"

namespace cursiv {

// None where the rows and the states do not both fit; `forced`, when not
// zero, is the row width asked for.
constexpr std::optional<StampLayout> stamp_split(std::uint32_t rows,
                                                    std::uint32_t states,
                                                    unsigned forced = 0) {
  const unsigned total = StampLayout::record_bits();
  const unsigned full = StampLayout{}.rowBits;
  const auto stateBits = static_cast<unsigned>(std::bit_width(states - 1u));
  const auto rowNeed = static_cast<unsigned>(std::bit_width(rows));
  if (stateBits > total) return std::nullopt;
  const unsigned rowBits =
      forced != 0 ? forced : std::min(full, total - stateBits);
  if (rowBits > full || stateBits > total - rowBits) return std::nullopt;
  if (rowNeed > rowBits) return std::nullopt;
  return StampLayout{rowBits};
}

// The limit that refused, named, where no split holds.
inline std::string stamp_refusal(std::uint32_t rows, std::uint32_t states,
                                    unsigned forced = 0) {
  std::string out = std::format(
      "its {} AST write chains need {} bits and its {} parser states {} "
      "bits, more than the {} bits of a record",
      rows, std::bit_width(rows), states, std::bit_width(states - 1u),
      StampLayout::record_bits());
  if (forced != 0)
    out += std::format(" with the chains forced to {} bits", forced);
  return out;
}

}  // namespace cursiv
