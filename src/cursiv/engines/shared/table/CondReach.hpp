// How far one condition reads the token in hand, which a completion compares
// with the text it replaces.
#pragma once

#include <cstdint>

#include "cursiv/core/text/TextOffset.hpp"

namespace cursiv {

struct CondReach {
  // From the token's first byte, at most; ~0 where a run is unbounded.
  TextOffset look = 0;
  // Past the end of a token with a width, whose bytes bound what a run
  // consumes.
  TextOffset tail = 0;
  // A phrase reads on past its token, over trivia of any length: how far it
  // reads is asked of the text, and `look` and `tail` count the rest.
  bool phrase = false;
  // The whole terminals such a phrase reads, the token in hand among them:
  // the tokens it can reach lie that many tokens on, whatever the bytes
  // between. `kUnboundedTokens` where a run of terminals has no end.
  std::uint16_t tokens = 0;
};

inline constexpr std::uint16_t kUnboundedTokens = 0xffffu;

}  // namespace cursiv
