// A grammar's image read into productions.
#pragma once

#include "cursiv/engine/grammar/Cfg.hpp"
#include "cursiv/engine/grammar/LowerDesc.hpp"

namespace cursiv::engine::gen {

// The productions of the grammar `image` shows, each record carrying the
// actions `desc` publishes, by their number there. Throws `Refused` for what
// the engine does not read.
Cfg lower(GrammarImage& image, const LowerDesc& desc);

}  // namespace cursiv::engine::gen
