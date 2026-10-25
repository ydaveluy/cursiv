// The terminal set of a DFA state, pooled: two states accepting the same
// terminals name one set.
#pragma once

#include <array>
#include <cstdint>

#include "cursiv/engines/lr/table/LrBitSet.hpp"
#include "cursiv/engines/lr/table/LrSizes.hpp"

namespace cursiv {

template <LrSizes S, unsigned NIdx, unsigned NPool>
struct LrSetStore {
  using TermSet = LrBitSet<S.termBits()>;

  std::array<TermSet, NPool> pool{};
  std::array<std::uint16_t, NIdx> of{};

  constexpr const TermSet& operator[](std::uint16_t s) const {
    return pool[of[s]];
  }
};

}  // namespace cursiv
