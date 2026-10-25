// The lookahead automaton of one choice: a state per set of readings still
// standing, an edge per token that tells them apart. The construction fills
// one where the exploration closes under its ceiling.
#pragma once

#include <cstdint>

#include "cursiv/engines/shared/table/WriteKind.hpp"

namespace cursiv {

// "The construction closed no automaton here."
inline constexpr std::uint32_t kLlNoDfa = 0xFFFFFFFFu;

struct LlDfaState {
  // The arm every reading here names, `kNoRow` while more than one does.
  std::uint16_t alt = kNoRow;
  std::uint32_t edge = 0, nedges = 0;
  // A position the readings here stand before, whose configuration the next
  // token is read under: `kNoRow` where the grammar has one configuration.
  std::uint16_t node = kNoRow;

  constexpr bool operator==(const LlDfaState&) const = default;
};

struct LlDfaEdge {
  std::uint16_t term = 0;
  std::uint32_t target = 0;

  constexpr bool operator==(const LlDfaEdge&) const = default;
};

}  // namespace cursiv
