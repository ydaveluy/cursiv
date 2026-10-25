// What may be written in each state: the terminals it shifts. What a reduce
// would go on to admit is the caret's replay to find.
#pragma once

#include <array>
#include <cstdint>

#include "cursiv/core/grammar/ElementId.hpp"

namespace cursiv {

// One continuation and its canonical record row, `kNoElement` where no member
// could be determined. Where a guard's landings write different members, the
// state the shift lands in names the entry that is theirs.
struct LrExpect {
  ElementId feature = kNoElement;
  ElementId site = kNoElement;
  ElementId landing = kNoElement;
};

// One row per state over a pool of the distinct continuations.
template <unsigned NStates, unsigned NItems, unsigned NPool>
struct LrExpectTable {
  static_assert(NPool <= 0x10000u, "a continuation is named by 16 bits");
  std::array<std::uint32_t, NStates + 1> base{};
  std::array<std::uint16_t, NItems> items{};
  std::array<LrExpect, NPool> pool{};
  // The terminal shifted into each state: minimisation keeps different
  // incoming terminals apart, so a leaf's state names it.
  std::array<std::uint16_t, NStates> shiftTerm{};
};

}  // namespace cursiv
