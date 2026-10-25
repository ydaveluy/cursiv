// The decisions each production wrote, in the order a reading taken top down
// meets them: what a merge compares two derivations by.
#pragma once


#include "cursiv/engines/lr/gen/LrBuildTable.hpp"

namespace cursiv {

// Lays `electAt`, `electWord` and `electPos`: the order a cell forks in reads
// them too.
void lr_elect_words(LrBuildTable& A);

// Lays what a merge reads beside them, once the words stand: the runs of
// pass-throughs the table skips and the productions fused states twin.
void lr_elect_runs(LrBuildTable& A);

// The symbol each state stands after, and the one the state under it stands
// after, read before any pass skips a pass-through: `stateSym`, `stateBelow`.
void lr_state_symbols(LrBuildTable& A);

}  // namespace cursiv
