// A conflict cell the second token decides: two arms whose acceptance sets
// are disjoint after consuming the cell's terminal, each read under one lex
// mode. The drive peeks instead of forking; a peek neither set claims still
// forks.
#pragma once

#include <cstdint>

#include "cursiv/engines/shared/table/DynSet.hpp"

namespace cursiv {

struct LrPeek {
  std::uint16_t state = 0;
  std::uint16_t term = 0;
  std::uint16_t mode[2] = {0xffffu, 0xffffu};
  DynSet set[2]{};
};

}  // namespace cursiv
