// The construction over a lowered program. It reads no grammar type, so its
// body compiles once for every language rather than once per language.
#pragma once

#include <cstdint>
#include <vector>

#include "cursiv/engines/ll/gen/LlBuiltProgram.hpp"
#include "cursiv/engines/ll/gen/LlLowered.hpp"
#include "cursiv/engines/ll/table/LlAtnView.hpp"

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

// The rules that two ways out of one choice can call before a token is read:
// a closure reaches them over two stacks, and the body of each is read again
// unless the callers share it. A rule that can end unread is not shared: its
// callers join only after it has left.
std::vector<std::uint8_t> ll_shared_rules(const LlAtnView& atn,
                                          std::uint16_t nrules);

}  // namespace cursiv
