// The action and goto matrices, planned once from the automaton's
// frozen rows.
#pragma once

#include <cstdint>
#include <vector>

#include "cursiv/engines/lr/gen/LrProgramMatrix.hpp"
#include "cursiv/engines/lr/gen/LrBuildTable.hpp"

namespace cursiv {

struct LrProgramLayout {
  explicit LrProgramLayout(const LrBuildTable& a);

  LrProgramMatrix actions;
  LrProgramMatrix gotos;
  std::vector<std::uint32_t> ops;
  // Per op, whether its cell is ordered as the election orders it.
  std::vector<std::uint64_t> exact;
  unsigned countBits = 1;
};

}  // namespace cursiv
