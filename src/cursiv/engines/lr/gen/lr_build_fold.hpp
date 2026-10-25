// The fold every reduce answers to: which chain a nonterminal hands back, and
// which production has bookkeeping to do at all.
#pragma once

#include "cursiv/engines/lr/gen/LrLowered.hpp"
#include "cursiv/engines/lr/gen/LrBuildTable.hpp"

namespace cursiv {

// False where the pool crossed the element-id domain: the caller stops there.
bool lr_fold_chains(LrBuildTable& A, const LrLowered& low);

// The fixed work of each final production, after its possible folds are known.
void lr_derive_reductions(const LrBuildTable& a, LrDerivedRows& rows);

}  // namespace cursiv
