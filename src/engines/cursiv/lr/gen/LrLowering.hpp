// A user grammar, lowered to the CFG the LR construction reads. The result is
// a closed value: no template of the grammar survives in it.
#pragma once

#include "cursiv/lr/gen/LrLowered.hpp"
#include "cursiv/lr/gen/LrRewrite.hpp"
#include "cursiv/shared/gen/LowerDescOf.hpp"
#include "cursiv/shared/gen/LoweringShell.hpp"

namespace cursiv::engine {

// The stages this lowering reads the rules through, as the descriptors the
// image and the lowering read.
template <class G>
struct LrView : LoweringView<G, LrRewrite, true> {};

template <class G>
inline constexpr LowerDesc kLrLower = lower_desc<LrView<G>>();

template <class G>
using LrLowering = LoweringShell<LrView<G>, LrLowered, &lr_lower, kLrLower<G>>;

}  // namespace cursiv::engine
