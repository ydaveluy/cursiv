// A user grammar, lowered to the program a predictive descent runs. The shape
// an author wrote survives -- a repetition stays a loop -- and no template of
// the grammar does; what a reader needs is in the program or nowhere.
#pragma once

#include "cursiv/core/grammar/ImageOf.hpp"
#include "cursiv/engines/ll/gen/LlLowered.hpp"
#include "cursiv/engines/ll/gen/LlRewrite.hpp"
#include "cursiv/engines/shared/gen/LowerDescOf.hpp"
#include "cursiv/engines/shared/gen/LoweringShell.hpp"

namespace cursiv {

// The stages this lowering reads the rules through, as the descriptors the
// image and the lowering read.
template <class G>
struct LlView : detail::LoweringView<G, detail::llow::Rewrite, false> {};

namespace detail {

template <class V, class R, class E, bool Step, bool Hands>
inline constexpr ImageDesc kNode<V, llow::Closed<R, E, Step, Hands>> =
    Draft(ImageKind::closed)
        .flags(static_cast<std::uint8_t>((Step ? 1 : 0) | (Hands ? 2 : 0)))
        .kids(1, kKids<V, E>)
        .rule(&kRule<V, declared_of_t<R>>);

template <class V, class E>
inline constexpr ImageDesc kNode<V, llow::Turns<E>> =
    Draft(ImageKind::turns).kids(1, kKids<V, E>);

}  // namespace detail

template <class G>
inline constexpr LowerDesc kLlLower = detail::lower_desc<LlView<G>>();

template <class G>
using LlLowering =
    detail::LoweringShell<LlView<G>, LlLowered, ll_lower, kLlLower<G>>;

}  // namespace cursiv
