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

// How far past its best match a walk must have read before the lexer treats
// the token as one that backtracked: the tail it then shares as a suffix. The
// walks, the suffix session and the generated walks all test this one value.
inline constexpr TextOffset kBacktrackTail = 64;

struct Token {
  TextOffset begin = 0;
  TextOffset end = 0;
  SymbolId sym = kInvalidSymbol;
  // The alphabet a lexer read it with, `kNoMode` for a token it did not.
  std::uint16_t mode = kNoMode;
  // The same token is the same span of the same symbol, however it was read:
  // `mode` is deliberately left out (the LL reader compares a held lookahead
  // with a fresh read, `ll_read.hpp`, and the modes may differ).
  constexpr bool operator==(const Token& o) const {
    return begin == o.begin && end == o.end && sym == o.sym;
  }
};

}  // namespace cursiv
