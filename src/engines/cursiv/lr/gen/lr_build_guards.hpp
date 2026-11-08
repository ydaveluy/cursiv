// Conditions leave the right-hand sides: each one guards the position it
// stands before, or the reduction when nothing follows it.
#pragma once

#include "cursiv/lr/gen/LrBuildCfg.hpp"
#include "cursiv/shared/gen/ChainPool.hpp"

namespace cursiv::engine {

// Rewrites the positions in place; a condition's own empty rule stays, named
// by nothing. False when joining two conditions ran out of ids.
bool lr_guard_positions(LrBuildCfg& g, ChainPool& writes);

}  // namespace cursiv::engine
