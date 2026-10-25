// The lookahead, and the two numbers the automaton's tables are stated in.
#pragma once

#include <cstdint>

#include "cursiv/core/text/TextOffset.hpp"

namespace cursiv {

// A grammar symbol as the tables number it. The runtime carries the number and
// never the meaning: terminals below the nonterminal base, which the
// construction owns.
using SymbolId = std::uint16_t;
inline constexpr SymbolId kInvalidSymbol = 0xFFFFu;
// The tables' contract: terminal 0 is end of input. A construction that
// numbers differently makes every state's `$end` action unreachable.
inline constexpr SymbolId kEofSymbol = 0;
// The same contract for the lexer's graph: a DFA edge that leads nowhere.
inline constexpr std::uint16_t kDeadState = 0xFFFFu;
// The mode a held lookahead was read in, where none is held.
inline constexpr std::uint16_t kNoMode = 0xFFFFu;

struct Token {
  TextOffset begin = 0;
  TextOffset end = 0;
  SymbolId sym = kInvalidSymbol;
  // The alphabet a lexer read it with, `kNoMode` for a token it did not.
  std::uint16_t mode = kNoMode;
  constexpr bool operator==(const Token& o) const {
    return begin == o.begin && end == o.end && sym == o.sym;
  }
};

}  // namespace cursiv
