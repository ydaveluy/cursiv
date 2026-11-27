// A member that holds one value is written once: no path of a rule writes it
// twice into one node.
#pragma once

#include <cstddef>

#include "cursiv/engine/grammar/LowerDesc.hpp"
#include "cursiv/engine/grammar/GrammarImage.hpp"

namespace cursiv::engine::gen {

// Refuses a second write to a single-valued property of the same node.
// Returns the number of branches allocated for the rules’ shared write sets.
std::size_t check_writes_once(GrammarImage& image, const LowerDesc& desc);

}  // namespace cursiv::engine::gen
