// The whole output of the lowering: the program, and what the tree owes at
// each of its positions. One value, built once.
#pragma once

#include <cstdint>
#include <vector>

#include "cursiv/ll/gen/LlLowCfg.hpp"
#include "cursiv/shared/gen/ChainPool.hpp"
#include "cursiv/shared/gen/GrammarFault.hpp"

namespace cursiv {

namespace detail {
struct GrammarImage;
}  // namespace detail
struct LowerDesc;

struct LlLowered {
  LlLowCfg cfg{};
  ChainPool writes{};
  // A nest starts a separate reduction region without returning a node yet.
  std::vector<std::uint8_t> nestChains{};
  // The first refusal this grammar earned, recorded instead of aborted.
  GrammarFault fault{};
};

LlLowered ll_lower(detail::GrammarImage& image, const LowerDesc& desc);

}  // namespace cursiv
