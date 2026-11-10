// A user grammar, lowered to the program a predictive descent runs. The
// author's shape survives (a repetition stays a loop) but no template of the
// grammar does: what a reader needs is in the program or nowhere.
#pragma once

#include "cursiv/grammar/ImageOf.hpp"
#include "cursiv/ll/gen/LlLowered.hpp"
#include "cursiv/ll/gen/LlRewrite.hpp"
#include "cursiv/shared/gen/LowerDescOf.hpp"
#include "cursiv/shared/gen/LoweringShell.hpp"

namespace cursiv::engine {

// The stages this lowering reads the rules through, as the descriptors the
// image and the lowering read.
template <class G>
struct LlView : LoweringView<G, llow::Rewrite, false> {};

}  // namespace cursiv::engine

namespace cursiv::detail {

template <class V, class R, class E, bool Step, bool Hands>
inline constexpr ImageDesc kNode<V, engine::llow::Closed<R, E, Step, Hands>> =
    Draft(ImageKind::closed)
        .flags(static_cast<std::uint8_t>((Step ? 1 : 0) | (Hands ? 2 : 0)))
        .children(1, kChildren<V, E>)
        .rule(&kRule<V, declared_of_t<R>>);

template <class V, class E>
inline constexpr ImageDesc kNode<V, engine::llow::Turns<E>> =
    Draft(ImageKind::turns).children(1, kChildren<V, E>);

}  // namespace cursiv::detail

namespace cursiv::engine {

template <class G>
inline constexpr LowerDesc kLlLower = lower_desc<LlView<G>>();

template <class G>
using LlLowering = LoweringShell<LlView<G>, LlLowered, &ll_lower, kLlLower<G>>;

}  // namespace cursiv::engine
