// The automaton of a built LR table as the shared lexer code reads it.
#pragma once

#include <cstdint>

#include "cursiv/lr/gen/LrBuildTable.hpp"
#include "cursiv/shared/gen/LexFirstByte.hpp"
#include "cursiv/shared/gen/lex_intern_set.hpp"
#include "cursiv/shared/table/LexRelay.hpp"

namespace cursiv::engine {

struct LrLexView : LexTableView<LrBuildTable> {
  bool bounded(std::uint16_t s) const { return a.dfa_bounded(s); }
  // Whether a state also accepts a terminal that stands only where no byte
  // follows: its token is read there with the others, not alone.
  bool ends(std::uint16_t s) const {
    for (const unsigned t : a.dfa_acc_set(s).members())
      if (t != 0 && lex_bound_ends(a.cfg, a.dfa_acc_bound(s, t))) return true;
    return false;
  }
  std::uint16_t relay(std::uint16_t s) const {
    return a.nrelays != 0 ? a.dfaRelay[s] : kNoRelay;
  }
  unsigned tier(std::uint16_t s, unsigned t) const { return a.dfa_tier(s, t); }
};

}  // namespace cursiv::engine
