// What a walk holds at a position: the match in hand. One rule reads a state
// into it, for a mode's walk and for the rest of every relayed token alike.
#pragma once

#include <algorithm>
#include <cstdint>

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/core/utils/macros.hpp"
#include "cursiv/engines/shared/table/LexRelay.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

// A boundary or an echo may accept inside a run and fail at its end.
constexpr bool lex_run_blind(bool accepting, bool bounded, bool relay) {
  return !relay && (!accepting || !bounded);
}

// A run crosses its positions blind, so it may only cross positions that
// cannot outlive its end. An unbounded accept is dominated by the accept at
// the run's end; a boundary reading the next byte is not -- it can admit
// inside the run and refuse at its end -- so there the walk stays per byte.
template <class Ad>
CURSIV_ALWAYS_INLINE inline bool lex_run_blind(const Ad& ad,
                                               std::uint16_t state) {
  bool relay = false;
  if constexpr (requires { ad.relay(state); })
    relay = ad.relay(state) != kNoRelay;
  return lex_run_blind(ad.accept(state) != 0, ad.bounded(state), relay);
}

// The state's own boundary refuses what follows -- a keyword's word class, or
// whatever else the bound set was built from.
template <class Ad>
CURSIV_ALWAYS_INLINE inline bool lex_bound_refuses(const Ad& ad,
                                                   std::uint16_t state,
                                                   const char* src,
                                                   TextOffset i,
                                                   TextOffset len) {
  return ad.bounded(state) && i < len && ad.bound_has(state, src, i, len);
}

struct LexMatch {
  TextOffset end;
  SymbolId term;
  // The state the match was read in, so a reading ranked against it need not
  // walk the token again.
  std::uint16_t state = kDeadState;

  // Reads state `st` at `i`; `far` covers what a boundary decoded past `i`.
  template <class Ad>
  CURSIV_ALWAYS_INLINE void take(const Ad& ad, std::uint16_t st,
                                 const char* src, TextOffset i, TextOffset len,
                                 TextOffset& far) {
    if (ad.accept(st) == 0) return;
    SymbolId t = static_cast<SymbolId>(ad.accept(st) - 1u);
    // The state's own election refused: the terminals it outranked answer
    // for themselves, and the one that stands takes the match.
    if constexpr (requires { ad.qualified(st, src, i, len, far); }) {
      t = ad.qualified(st, src, i, len, far);
      if (t == kInvalidSymbol) return;
    } else {
      if (i < len && ad.bounded(st))
        far = std::max(far, ad.bound_reach(st, src, i, len));
      if (lex_bound_refuses(ad, st, src, i, len)) {
        t = ad.refused(st, src, i, len);
        if (t == kInvalidSymbol) return;
      }
    }
    end = i;
    term = t;
    state = st;
  }
};
static_assert(sizeof(LexMatch) == 8, "the state fills the padding");

}  // namespace cursiv
