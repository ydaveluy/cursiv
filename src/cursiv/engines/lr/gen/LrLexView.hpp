// The automaton of a built LR table as the shared lexer code reads it.
#pragma once

#include <cstdint>
#include <string_view>

#include "cursiv/engines/lr/gen/LrBuildTable.hpp"
#include "cursiv/engines/shared/gen/LexRelayGen.hpp"
#include "cursiv/engines/shared/gen/lex_intern_set.hpp"
#include "cursiv/engines/shared/table/TermInfo.hpp"

namespace cursiv {

struct LrLexView {
  const LrBuildTable& a;

  std::uint32_t modes() const { return a.nmodes; }
  std::uint16_t start(std::uint32_t m) const { return a.modeStart[m]; }
  unsigned classes() const { return a.ncls; }
  std::uint8_t cls(unsigned b) const { return a.cls[b]; }
  std::uint16_t next(std::uint16_t s, unsigned c) const {
    return a.next_dfa(s, static_cast<std::uint8_t>(c));
  }
  std::uint16_t accept(std::uint16_t s) const { return a.dfaAccept[s]; }
  bool bounded(std::uint16_t s) const { return a.dfa_bounded(s); }
  // Whether a state also accepts a terminal that stands only where no byte
  // follows: its token is read there with the others, not alone.
  bool ends(std::uint16_t s) const {
    for (unsigned t = 1; t < a.cfg.nterms; ++t)
      if (a.dfa_acc_set(s).test(t) &&
          lex_bound_ends(a.cfg, a.dfa_acc_bound(s, t)))
        return true;
    return false;
  }
  std::uint16_t relay(std::uint16_t s) const {
    return a.nrelays != 0 ? a.dfaRelay[s] : kNoRelay;
  }
  const LexRelayGen& relay_of(std::uint16_t k) const { return a.relays[k]; }
  unsigned tier(std::uint16_t s, unsigned t) const { return a.dfa_tier(s, t); }
  const TermInfo& term(unsigned t) const { return a.cfg.terms[t]; }
  std::string_view text(unsigned t) const {
    return a.cfg.term_text(static_cast<std::uint16_t>(t));
  }
  bool folded(unsigned t) const { return a.cfg.termBuild[t].folded; }
};

}  // namespace cursiv
