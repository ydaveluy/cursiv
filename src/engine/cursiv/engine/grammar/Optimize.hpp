// A lowered grammar rewritten to reduce less: it reads the same texts and
// builds the same trees.
#pragma once

#include "cursiv/engine/grammar/Cfg.hpp"

namespace cursiv::engine::gen {

// Rewrites the productions of `cfg`: none is added, removed or given to
// another nonterminal.
void optimize(Cfg& cfg);

}  // namespace cursiv::engine::gen
