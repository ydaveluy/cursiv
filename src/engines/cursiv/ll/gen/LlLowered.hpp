// The whole output of the lowering: the program, and what the tree writes at
// each of its positions. One value, built once.
#pragma once

#include <cstdint>
#include <vector>

#include "cursiv/ll/gen/LlLowCfg.hpp"
#include "cursiv/shared/gen/ChainPool.hpp"
#include "cursiv/shared/gen/GrammarFault.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"

namespace cursiv::engine {

struct LowerDesc;

struct LlLowered {
  LlLowCfg cfg{};
  ChainPool writes{};
  // The first refusal of this grammar, recorded instead of aborting.
  GrammarFault fault{};
};

LlLowered ll_lower(GrammarImage& image, const LowerDesc& desc);

}  // namespace cursiv::engine
