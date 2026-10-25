// The LR automaton, built from a lowered grammar. The construction reads no
// grammar type, so it compiles once for every language.
#pragma once

#include "cursiv/engines/lr/gen/LrBuildTable.hpp"
#include "cursiv/engines/lr/gen/LrLowered.hpp"

namespace cursiv {

// The passes that rewrite a merged table, skipped where a lane reads the
// table as the merge laid it.
struct LrBuildOptions {
  bool passes = true;
};

LrBuildTable lr_build_automaton(const LrLowered& low, LrBuildOptions opt = {});
// The wider graph, whose states carry their kernel lookaheads; given the
// narrower one, no further than its merge where it does not stand against it.
LrBuildTable lr_build_wide(const LrLowered& low,
                           const LrBuildTable* against = nullptr);

// Whether the wider graph owes fewer ambiguities, no cap given way.
bool lr_wider_stands(const LrBuildTable& c, const LrBuildTable& w);

}  // namespace cursiv
