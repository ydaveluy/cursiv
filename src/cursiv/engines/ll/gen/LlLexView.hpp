// The automaton of a built LL program as the shared lexer code reads it.
#pragma once

#include <algorithm>
#include <cstdint>

#include "cursiv/engines/ll/gen/LlBuiltProgram.hpp"
#include "cursiv/engines/shared/gen/LexFirstByte.hpp"
#include "cursiv/engines/shared/table/LexRelay.hpp"
#include "cursiv/engines/shared/table/TermInfo.hpp"

namespace cursiv {

struct LlLexView : LexTableView<LlBuiltProgram> {
  // A state whose election checks its acceptances reads past the token: one
  // that also accepts a terminal standing only where no byte follows does.
  bool bounded(std::uint16_t s) const {
    return a.dfaBound[s] != kNoSet || a.lexChecks[s] != 0;
  }
  std::uint16_t relay(std::uint16_t s) const {
    return a.relays.empty() ? kNoRelay : a.dfaRelay[s];
  }
  unsigned tier(std::uint16_t s, unsigned t) const {
    unsigned out = 0;
    for (const LlLexAccept& x : a.choices(s))
      if (x.term == t) out = std::max<unsigned>(out, x.tier);
    return out;
  }
};

// Whether mode `m` reads terminal `t` off its one byte alone, as the table's
// byte rows say. A mode that lexes for length alone reads every token through
// its walk.
inline bool ll_byte_term(const LlBuiltProgram& a, unsigned m, unsigned t) {
  return m < a.nmodes && (a.modeLonger.empty() || a.modeLonger[m] == kNoMode) &&
         lex_byte_literal(LlLexView{a}, m, t);
}

}  // namespace cursiv
