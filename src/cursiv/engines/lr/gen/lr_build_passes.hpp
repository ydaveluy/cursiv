// The passes that rewrite a fused table into a shape the parse reads faster,
// never another language. Each reads what the one before it left.
#pragma once

#include <cstdint>
#include <vector>

#include "cursiv/engines/lr/gen/LrBuildCfg.hpp"
#include "cursiv/engines/lr/gen/LrItem.hpp"
#include "cursiv/engines/lr/gen/LrBuildTable.hpp"
#include "cursiv/engines/shared/gen/LexModes.hpp"

namespace cursiv {

// The terminal shifted into each state: every rewrite keeps it one.
std::vector<std::uint16_t> lr_shift_terms(const LrBuildTable& a);

// A pass-through the elimination below may skip, less what a frame copy
// cannot restate.
bool lr_unit_passes(const LrBuildCfg& g, unsigned p);

// Substitutes a unit production's callee into its caller. Each new state
// keeps its root's lexical mode.
void lr_unit_elimination(LrBuildTable& A, const LexModes& modes,
                         const std::vector<std::uint8_t>& cfg);

void lr_close_rows(LrBuildTable& A, const LrPoolRows<LrItem>& clos,
                   const std::vector<unsigned>& pick);

// What makes two reductions one for the states that fuse: their fixed
// grammar data, not their symbols, nor the configurations their positions
// were read under, which no reduction reads again.
void lr_reduce_key(const LrBuildCfg& g, unsigned p,
                   std::vector<std::uint32_t>& key);

void lr_minimize_states(LrBuildTable& A);

void lr_peek_oracle(LrBuildTable& A);

void lr_subsume_modes(LrBuildTable& A, const LexModes& modes);

// Equal rows of actions are seated once, the states naming the same one.
void lr_share_action_rows(LrBuildTable& A);

}  // namespace cursiv
