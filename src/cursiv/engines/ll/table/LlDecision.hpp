// One choice the parse makes, and how the construction settled it. A decision
// the analysis separates is a switch in the emitted code; one it cannot is an
// episode at run time.
#pragma once

#include <cstdint>

#include "cursiv/engines/ll/table/LlDfa.hpp"
#include "cursiv/engines/shared/table/WriteKind.hpp"

namespace cursiv {

// `single` names an arm one token tells apart, which is a switch; `staged`
// walks an automaton the construction closed; `adaptive` is the episode's.
enum class LlPredict : std::uint8_t { single, staged, adaptive };

struct LlDecision {
  // The position it decides. How many arms it decides between, and what its
  // repair costs, stand in `LlDecisionBuild`: the emitted program spells the
  // arms, so a descent never counts them.
  std::uint16_t node = kNoRow;
  LlPredict how = LlPredict::single;
  // One row of terminals per arm, which is the whole of a `single`.
  std::uint32_t set = 0;
  // ...and the automaton a `staged` one walks, `kLlNoDfa` where the
  // exploration did not close.
  std::uint32_t dfa = kLlNoDfa;
  // The alphabet the lexer reads the deciding token with.
  std::uint16_t mode = kNoRow;
  // The ATN state the arms leave, which is what a simulation starts from.
  std::uint32_t state = 0;
  // The rule it stands in, for what a refusal names.
  std::uint16_t rule = kNoRow;
  // The arm a repair takes where no token answers: the one that costs the
  // fewest invented tokens. A choice whose arms are all expensive still has
  // a cheapest, and taking it beats matching nothing.
  std::uint16_t repair = 0;

  constexpr bool operator==(const LlDecision&) const = default;
};

}  // namespace cursiv
