// The lookahead automaton of one choice: a state per set of readings still
// standing, an edge per token that tells them apart. The construction fills
// one where the exploration closes under its ceiling.
#pragma once

#include <cstdint>

#include "cursiv/shared/table/WriteKind.hpp"

namespace cursiv::engine {

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

// A terminal the readings admit that settles no arm however far it reads: the
// walk reads under it, and hands the choice over where the text spells it.
inline constexpr std::uint32_t kLlDfaHandOver = ~std::uint32_t{0};

struct LlDfaEdge {
  std::uint16_t term = 0;
  std::uint32_t target = 0;

  constexpr bool operator==(const LlDfaEdge&) const = default;
};

}  // namespace cursiv::engine
