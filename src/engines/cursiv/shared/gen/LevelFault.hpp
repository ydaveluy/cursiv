// The operators of a precedence table compared level against level: a token
// string two levels share reads the text around it two ways.
#pragma once

#include <cstdint>
#include <span>

#include "cursiv/grammar/GrammarImage.hpp"
#include "cursiv/shared/gen/GrammarFault.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"

namespace cursiv::engine {

// The first refusal of the tables of `rules`, a rule named by its position in
// `rules` plus one; none where every table is sound.
GrammarFault level_fault(GrammarImage& image,
                         std::span<const std::uint32_t> rules);

}  // namespace cursiv::engine
