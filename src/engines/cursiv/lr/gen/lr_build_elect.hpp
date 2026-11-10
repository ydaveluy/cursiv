// The decisions each production wrote, in the order a reading taken top down
// reaches them: what a merge compares two derivations by.
#pragma once

#include "cursiv/lr/gen/LrBuildTable.hpp"

namespace cursiv::engine {

// Fills `electAt`, `electWord` and `electPos`: the order a cell forks in reads
// them too.
void lr_elect_words(LrBuildTable& A);

// Fills what a merge reads beside them, once the words stand: the runs of
// pass-throughs the table skips and the productions fused states twin.
void lr_elect_runs(LrBuildTable& A);

// The symbol each state stands after, and the one the state under it stands
// after, read before any pass skips a pass-through: `stateSym`, `stateBelow`.
void lr_state_symbols(LrBuildTable& A);

}  // namespace cursiv::engine
