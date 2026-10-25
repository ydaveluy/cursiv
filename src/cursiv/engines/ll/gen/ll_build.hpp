// The construction over a lowered program. It reads no grammar type, so its
// body compiles once for every language rather than once per language.
#pragma once

#include <cstdint>

#include "cursiv/engines/ll/gen/LlBuiltProgram.hpp"
#include "cursiv/engines/ll/gen/LlLowered.hpp"

namespace cursiv {

// Every optimisation the construction makes, switchable: an attribution runs
// the generator twice and diffs the two directories.
struct LlBuildOptions {
  // How many tokens the static separation of a choice may look at before the
  // decision is left to the simulation. What stops the exploration is the
  // caller it does not know, not this ceiling.
  std::uint8_t depth = 4;
  // Separate what one token already separates. Off, every choice is adaptive,
  // which is what an attribution of the prediction measures against.
  bool staticPredict = true;
};

LlBuiltProgram ll_build_program(const LlLowered& low, LlBuildOptions opt = {});

}  // namespace cursiv
