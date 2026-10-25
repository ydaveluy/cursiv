// One walk over a mode's automaton, for every engine: longest match, ties to
// the earliest declared terminal, and a state's own boundary tested inside the
// match rather than in a second scan. An engine names its table through an
// adapter; the search is written once.
#pragma once

#include <algorithm>
#include <cstdint>
#include <type_traits>

#include "cursiv/core/utils/macros.hpp"
#include "cursiv/engines/shared/runtime/LexMatch.hpp"
#include "cursiv/engines/shared/runtime/LexRelayRun.hpp"
#include "cursiv/engines/shared/runtime/Scan.hpp"
#include "cursiv/engines/shared/table/LexRelay.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

// The DFA cursor, independent of what a match retains. Both a single best
// match and a collection read each state through `take`; `skip` preserves
// the reader's own fast path over a self-loop. A generated prefix can hand
// over its match and relays without re-reading the prefix.
template <class Ad, class Match, class Relays, class Skip>
CURSIV_ALWAYS_INLINE inline void lex_walk_rows(const Ad& ad, const char* src,
                                               TextOffset len, TextOffset start,
                                               std::uint16_t st, TextOffset& i,
                                               TextOffset& far, Match& match,
                                               Relays& relays, Skip&& skip) {
  for (;;) {
    match.take(ad, st, src, i, len, far);
    if constexpr (requires { ad.relay(st); })
      for (auto k = ad.relay(st); k != kNoRelay; k = ad.relay_of(k).next)
        relays.launch(ad, k, src, len, start, i);
    if (i >= len) break;
    const auto to = ad.step(st, static_cast<unsigned char>(src[i]));
    if (to == kDeadState) break;
    ++i;
    if (to == st) skip(st, i);
    st = to;
  }
}

// `Ad` names one engine's table: `start`, `step`, `accept`, `bounded`,
// `bound_has`, `bound_reach`, and `refused` or, where a state's election
// answers for every acceptance, `qualified` -- and, where its tokens echo a
// mark, `relay`, `relay_of`, `sets`, `names`, `prefers`; where a terminal
// refuses words behind its head, `unrefused`. Each answers with a lookup.
//
// Inlined into the one entry point each engine gives it: a descent asks this
// once per token, and a frame between the two would be paid on every one.
// `halt` is where the walk stopped reading: `len` when the input ran out with
// the automaton still alive, so a caller splicing text knows the answer may
// still change past what it handed over.
template <class Ad>
CURSIV_ALWAYS_INLINE inline Scan lex_walk_from(Ad ad, const char* src,
                                               TextOffset len, std::uint16_t st,
                                               TextOffset from,
                                               TextOffset& halt) {
  TextOffset i = from;
  TextOffset far = from;
  LexMatch m{from, kInvalidSymbol};
  // A relay reads ahead of this DFA cursor: its searches run as the walk
  // meets them, the rests once it is done, apart from what it matched.
  constexpr bool relays = requires { ad.relay(st); };
  [[maybe_unused]] std::conditional_t<relays, LexRelayRun<Ad>, bool> run{};
  const auto skip = [&](std::uint16_t state, TextOffset& at) {
    if (!lex_run_blind(ad, state)) return;
    while (at < len &&
           ad.step(state, static_cast<unsigned char>(src[at])) == state)
      ++at;
  };
  lex_walk_rows(ad, src, len, from, st, i, far, m, run, skip);
  Scan out{m.end, m.term};
  if constexpr (relays) {
    if (run.any()) {
      Scan relayed{from, kInvalidSymbol};
      // A literal or keyword that stands where the walk ended reserves from a
      // relayed reading ending there what it reserves from the walk's own, and
      // a reading behind conditions that such a branch read takes the walk's
      // reading.
      bool asked = false;
      std::uint16_t ended = st;
      const auto stood = [&] {
        if (!asked) {
          asked = true;
          for (TextOffset k = from; k < out.end && ended != kDeadState; ++k)
            ended = ad.step(ended, static_cast<unsigned char>(src[k]));
        }
        return ended != kDeadState;
      };
      const auto reserved = [&](SymbolId t) {
        if constexpr (requires {
                        ad.reserves(ended, t, src, from, out.end, len, far);
                      }) {
          return stood() && ad.reserves(ended, t, src, from, out.end, len, far);
        } else {
          return false;
        }
      };
      auto tier = 0u;
      const auto taken = [&] {
        if constexpr (requires { ad.takes(ended, 1u, out.term); }) {
          return tier != 0 && out.term != kInvalidSymbol && stood() &&
                 ad.takes(ended, ad.taking(relayed.term, tier), out.term);
        } else {
          return false;
        }
      };
      const auto walked = [&] {
        return out.term != kInvalidSymbol && stood()
                   ? ad.tier(ended, out.term, src, out.end, len, far)
                   : 0u;
      };
      run.finish(ad, src, len, far, [&](SymbolId t, TextOffset e, unsigned as) {
        if constexpr (requires { ad.skips(t); })
          if (ad.skips(t)) return;
        if (e == out.end && out.term != kInvalidSymbol && reserved(t)) return;
        if (e == relayed.end && t == relayed.term) {
          tier = std::max(tier, as);
        } else if (e > relayed.end ||
                   (e == relayed.end &&
                    ad.prefers_relayed(t, as, relayed.term, tier))) {
          relayed = Scan{e, t};
          tier = as;
        }
      });
      if (relayed.end > out.end ||
          (relayed.end == out.end && relayed.term != kInvalidSymbol &&
           (taken() ||
            ad.prefers_relayed(relayed.term, tier, out.term, walked()))))
        out = relayed;
    }
  }
  if constexpr (requires { ad.unrefused(src, len, st, from, out, far); })
    out = ad.unrefused(src, len, st, from, out, far);
  halt = i > far ? i : far;
  return out;
}

template <class Ad>
CURSIV_ALWAYS_INLINE inline Scan lex_walk(Ad ad, const char* src,
                                          TextOffset len, std::uint16_t mode,
                                          TextOffset from, TextOffset& halt) {
  return lex_walk_from(ad, src, len, ad.start(mode), from, halt);
}

template <class Ad>
CURSIV_ALWAYS_INLINE inline Scan lex_walk(Ad ad, const char* src,
                                          TextOffset len, std::uint16_t mode,
                                          TextOffset from) {
  TextOffset halt;
  return lex_walk(ad, src, len, mode, from, halt);
}

template <class Ad>
CURSIV_ALWAYS_INLINE inline Scan lex_walk_from(Ad ad, const char* src,
                                               TextOffset len, std::uint16_t st,
                                               TextOffset from) {
  TextOffset halt;
  return lex_walk_from(ad, src, len, st, from, halt);
}

}  // namespace cursiv
