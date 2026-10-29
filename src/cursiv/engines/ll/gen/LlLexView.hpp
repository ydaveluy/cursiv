// The automaton of a built LL program as the shared lexer code reads it.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "cursiv/engines/ll/gen/LlBuiltProgram.hpp"
#include "cursiv/engines/shared/gen/LexFirstByte.hpp"
#include "cursiv/engines/shared/gen/LexRelayGen.hpp"
#include "cursiv/engines/shared/table/LexRelay.hpp"
#include "cursiv/engines/shared/table/TermInfo.hpp"

namespace cursiv {

struct LlLexView {
  const LlBuiltProgram& a;

  std::uint32_t modes() const { return a.nmodes; }
  std::uint16_t start(std::uint32_t m) const { return a.modeStart[m]; }
  unsigned classes() const { return a.ncls; }
  std::uint8_t cls(unsigned b) const { return a.cls[b]; }
  std::uint16_t next(std::uint16_t s, unsigned c) const {
    return a.dfaNext[std::size_t{s} * a.ncls + c];
  }
  std::uint16_t accept(std::uint16_t s) const { return a.dfaAccept[s]; }
  // A state whose election checks its acceptances reads past the token: one
  // that also accepts a terminal standing only where no byte follows does.
  bool bounded(std::uint16_t s) const {
    return a.dfaBound[s] != kNoSet || a.lexChecks[s] != 0;
  }
  std::uint16_t relay(std::uint16_t s) const {
    return a.relays.empty() ? kNoRelay : a.dfaRelay[s];
  }
  const LexRelayGen& relay_of(std::uint16_t k) const { return a.relays[k]; }
  unsigned tier(std::uint16_t s, unsigned t) const {
    unsigned out = 0;
    for (const LlLexAccept& x : a.choices(s))
      if (x.term == t) out = std::max<unsigned>(out, x.tier);
    return out;
  }
  const TermInfo& term(unsigned t) const { return a.cfg.terms[t]; }
  std::string_view text(unsigned t) const {
    return a.cfg.term_text(static_cast<std::uint16_t>(t));
  }
  bool folded(unsigned t) const { return a.cfg.termBuild[t].folded; }
};

// Whether mode `m` reads terminal `t` off its one byte alone, as the table's
// byte rows say. A mode that lexes for length alone reads every token through
// its walk.
inline bool ll_byte_term(const LlBuiltProgram& a, unsigned m, unsigned t) {
  return m < a.nmodes && (a.modeLonger.empty() || a.modeLonger[m] == kNoMode) &&
         lex_byte_literal(LlLexView{a}, m, t);
}

}  // namespace cursiv
