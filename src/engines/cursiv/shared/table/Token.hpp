// The lookahead, and the two numbers the automaton's tables are stated in.
#pragma once

#include <cstdint>

#include "cursiv/text/TextOffset.hpp"

namespace cursiv::engine {

// A grammar symbol as the tables number it: terminals below the nonterminal
// base, which the construction sets.
using SymbolId = std::uint16_t;
inline constexpr SymbolId kInvalidSymbol = 0xFFFFu;
// The tables' contract: terminal 0 is end of input. A construction that
// numbers differently makes every state's `$end` action unreachable.
inline constexpr SymbolId kEofSymbol = 0;
// A DFA edge that leads nowhere.
inline constexpr std::uint16_t kDeadState = 0xFFFFu;
// The mode a held lookahead was read in, where none is held.
inline constexpr std::uint16_t kNoMode = 0xFFFFu;

// How far past its best match a walk must read before the lexer shares that
// tail as a suffix. Runtime and generated walks all test this one value.
inline constexpr TextOffset kBacktrackTail = 64;

struct Token {
  TextOffset begin = 0;
  TextOffset end = 0;
  SymbolId sym = kInvalidSymbol;
  // The alphabet a lexer read it with, `kNoMode` for a token it did not.
  std::uint16_t mode = kNoMode;
  // The same span of the same symbol. `mode` is left out: a held lookahead and
  // a fresh read of it may use different modes.
  constexpr bool operator==(const Token& o) const {
    return begin == o.begin && end == o.end && sym == o.sym;
  }
};

}  // namespace cursiv::engine
