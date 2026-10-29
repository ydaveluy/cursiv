// A user grammar, lowered to the CFG the LR construction reads. The result is
// a closed value: no template of the grammar survives in it.
#pragma once

#include "cursiv/engines/lr/gen/LrLowered.hpp"
#include "cursiv/engines/lr/gen/LrRewrite.hpp"
#include "cursiv/engines/shared/gen/LowerDescOf.hpp"
#include "cursiv/engines/shared/gen/LoweringShell.hpp"

namespace cursiv {

// The stages this lowering reads the rules through, as the descriptors the
// image and the lowering read.
template <class G>
struct LrView : detail::LoweringView<G, LrRewrite, true> {};

template <class G>
inline constexpr LowerDesc kLrLower = detail::lower_desc<LrView<G>>();

template <class G>
using LrLowering =
    detail::LoweringShell<LrView<G>, LrLowered, lr_lower, kLrLower<G>>;

}  // namespace cursiv
