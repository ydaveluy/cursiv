// A conflict cell the second token decides: two arms with disjoint acceptance
// sets after the cell's terminal, each under one mode. The drive peeks instead
// of forking; a peek neither set claims still forks.
#pragma once

#include <cstdint>

#include "cursiv/shared/table/DynSet.hpp"

namespace cursiv::engine {

struct LrPeek {
  std::uint16_t state = 0;
  std::uint16_t term = 0;
  std::uint16_t mode[2] = {0xffffu, 0xffffu};
  DynSet set[2]{};
};

}  // namespace cursiv::engine
