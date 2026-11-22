// A member that holds one value is written once: no path of a rule writes it
// twice into one node.
#pragma once

#include "cursiv/engine/grammar/LowerDesc.hpp"
#include "cursiv/engine/grammar/GrammarImage.hpp"

namespace cursiv::engine::gen {

// Throws `Refused`, naming the rule and the member, where a rule reached from
// the entry writes a member twice into one node: one after the other, at
// every turn of a repetition, or over the node a rule it calls built. A
// member written with `append` is written as often as it is read.
void check_writes_once(GrammarImage& image, const LowerDesc& desc);

}  // namespace cursiv::engine::gen
