// The tables derived from a built automaton, laid on the one that ships.
#pragma once

#include <span>

#include "cursiv/lr/gen/LrBuildTable.hpp"

namespace cursiv::engine {

// `sites` holds the canonical record rows; `kNoElement` names no member.
void lr_derive_tables(LrBuildTable& a, std::span<const ElementId> sites);

}  // namespace cursiv::engine
