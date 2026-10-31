// One walk over a mode's automaton, for every engine: longest match, ties to
// the earliest declared terminal, and a state's own boundary tested inside the
// match rather than in a second scan. An engine names its table through an
// adapter; the search is written once.
#pragma once

#include <algorithm>
#include <cstdint>
#include <type_traits>

#include "cursiv/core/utils/macros.hpp"
#include "cursiv/engines/shared/runtime/LexAdapter.hpp"
#include "cursiv/engines/shared/runtime/LexMatch.hpp"
#include "cursiv/engines/shared/runtime/LexRelayRun.hpp"
#include "cursiv/engines/shared/runtime/LexSuffixSession.hpp"
#include "cursiv/engines/shared/runtime/Scan.hpp"
#include "cursiv/engines/shared/table/LexRelay.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

template <class Ad>
inline constexpr bool kLexBacktrack = [] {
  if constexpr (requires { Ad::kRefuses; }) {
    if constexpr (requires { Ad::kBacktrack; }) return Ad::kBacktrack;
    return true;
  }
  return false;
}();

// The DFA cursor, independent of what a match retains. Both a single best
// match and a collection read each state through `take`; `skip` preserves
// the reader's own fast path over a self-loop. A generated prefix can hand
// over its match and relays without re-reading the prefix. A boolean `skip`
// can finish the walk at a checkpoint; a void `skip` only crosses self-loops.
template <class Ad, class Match, class Relays, class Skip>
CURSIV_ALWAYS_INLINE inline bool lex_walk_rows(const Ad& ad, const char* src,
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
    if constexpr (std::is_invocable_r_v<bool, Skip, std::uint16_t,
                                        TextOffset&, bool>) {
      if (to <= st && skip(to, i, to == st)) return true;
    } else if (to == st) {
      skip(st, i);
    }
    st = to;
  }
  return false;
}

// What a walk read once it stopped at `i`, begun at `from` in state `st`:
// its match, the relays it launched ranked against it, and a refused word
// read again. `halt` is where it stopped reading.
template <class Ad, class Relays>
CURSIV_ALWAYS_INLINE inline Scan lex_walk_settle(
    const Ad& ad, const char* src, TextOffset len, std::uint16_t st,
    TextOffset from, TextOffset i, TextOffset far, const LexMatch& m,
    [[maybe_unused]] Relays& run, TextOffset& halt) {
  constexpr bool relays = requires { ad.relay(st); };
  Scan out{m.end, m.term};
  if constexpr (relays) {
    if (run.any()) {
      Scan relayed{from, kInvalidSymbol};
      // A literal or keyword that stands where the walk ended reserves from a
      // relayed reading ending there what it reserves from the walk's own, and
      // a reading behind conditions that such a branch read takes the walk's
      // reading.
      const std::uint16_t ended = m.state;
      const auto reserved = [&](SymbolId t) {
        if constexpr (requires {
                        ad.reserves(ended, t, src, from, out.end, len, far);
                      }) {
          return ad.reserves(ended, t, src, from, out.end, len, far);
        } else {
          return false;
        }
      };
      auto tier = 0u;
      const auto taken = [&] {
        if constexpr (requires { ad.takes(ended, 1u, out.term); }) {
          return tier != 0 && out.term != kInvalidSymbol &&
                 ad.takes(ended, ad.taking(relayed.term, tier), out.term);
        } else {
          return false;
        }
      };
      const auto walked = [&] {
        return out.term != kInvalidSymbol
                   ? ad.tier(ended, out.term, src, out.end, len, far)
                   : 0u;
      };
      run.finish(ad, src, len, far, [&](SymbolId t, TextOffset e, unsigned as) {
        if constexpr (LexSkipping<Ad>)
          if (ad.skip->test(t)) return;
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
  if constexpr (LexRereads<Ad>)
    out = lex_unrefused(ad, src, len, st, from, out, far);
  halt = i > far ? i : far;
  return out;
}

// `Ad` names one engine's table: `start`, `step`, `accept`, `bounded`,
// `bound_has`, `bound_reach`, and `refused` or, where a state's election
// answers for every acceptance, `qualified` -- and, where its tokens echo a
// mark, `relay`, `relay_of`, `sets`, `names`, `prefers`; where a terminal
// refuses words behind its head, `Skipping`. Each answers with a lookup.
// What `kQualified` and `kRelays` announce is checked against these first
// (`LexAdapter.hpp`).
//
// Inlined into the one entry point each engine gives it: a descent asks this
// once per token, and a frame between the two would be paid on every one.
// `halt` is where the walk stopped reading: `len` when the input ran out with
// the automaton still alive, so a caller splicing text knows the answer may
// still change past what it handed over.
template <class Ad, bool Checkpoints = false>
CURSIV_ALWAYS_INLINE inline Scan lex_walk_from(Ad ad, const char* src,
                                               TextOffset len, std::uint16_t st,
                                               TextOffset from,
                                               TextOffset& halt) {
  static_assert(LexCursor<Ad>,
                "a lexical adapter offers start, step, accept and bounded");
  static_assert(LexAcceptanceAnnounced<Ad>,
                "an adapter that announces kQualified offers qualified(); one "
                "that does not offers the boundary fallback");
  static_assert(LexRelaysAnnounced<Ad>,
                "an adapter's kRelays matches its relay(), and a relay offers "
                "the whole relay protocol");
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
  constexpr bool checkpoints = Checkpoints && kLexBacktrack<Ad>;
  if constexpr (checkpoints) {
    bool queried = false;
    const auto checkpoint = [&](std::uint16_t state, TextOffset& at,
                                bool self) {
      // A return edge exposes every cycle. An unavailable cache leaves this
      // token's remaining scan on its ordinary path.
      if (!queried && m.term != kInvalidSymbol &&
          (ad.accept(state) == 0 || ad.bounded(state)) &&
          (self || (at > m.end && at - m.end > kBacktrackTail))) {
        if constexpr (relays) {
          if (run.any()) {
            if (self) skip(state, at);
            return false;
          }
        }
        queried = true;
        const auto suffix =
            LexSuffixSession<Ad>::continue_at(ad, src, len, state, at);
        if (suffix.available) {
          if (suffix.best.term != kInvalidSymbol) {
            m.end = suffix.best.end;
            m.term = suffix.best.term;
          }
          far = std::max(far, suffix.halt);
          at = suffix.halt;
          return true;
        }
      }
      if (self) skip(state, at);
      return false;
    };
    if (lex_walk_rows(ad, src, len, from, st, i, far, m, run, checkpoint)) {
      halt = far;
      return Scan{m.end, m.term};
    }
  } else {
    lex_walk_rows(ad, src, len, from, st, i, far, m, run, skip);
  }
  Scan out = lex_walk_settle(ad, src, len, st, from, i, far, m, run, halt);
  if constexpr (checkpoints)
    if (out.term != kInvalidSymbol && halt > out.end &&
        halt - out.end > kBacktrackTail)
      out = LexSuffixSession<Ad>::finish(ad, src, len, st, from, halt, out);
  return out;
}

template <class Ad>
CURSIV_ALWAYS_INLINE inline Scan lex_walk(Ad ad, const char* src,
                                          TextOffset len, std::uint16_t mode,
                                          TextOffset from, TextOffset& halt) {
  if constexpr (kLexBacktrack<Ad>) {
    if (LexSuffixSession<Ad>::shares(ad, mode)) {
      if (LexSuffixSession<Ad>::has_backtracked())
        return lex_walk_from<Ad, true>(ad, src, len, ad.start(mode), from,
                                       halt);
      auto out =
          lex_walk_from<Ad, false>(ad, src, len, ad.start(mode), from, halt);
      if (out.term != kInvalidSymbol && halt > out.end &&
          halt - out.end > kBacktrackTail)
        out = LexSuffixSession<Ad>::finish(ad, src, len, ad.start(mode), from,
                                           halt, out);
      return out;
    }
  }
  return lex_walk_from<Ad, false>(ad, src, len, ad.start(mode), from, halt);
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

// The end of a generated walk of `mode` begun at `from` and stopped at `i`:
// what `lex_walk` answers from there, where the session has not backtracked.
template <class Ad, class Relays>
CURSIV_ALWAYS_INLINE inline Scan lex_walk_end(const Ad& ad, const char* src,
                                              TextOffset len,
                                              std::uint16_t mode,
                                              TextOffset from, TextOffset i,
                                              TextOffset far, const LexMatch& m,
                                              Relays& run) {
  TextOffset halt;
  Scan out =
      lex_walk_settle(ad, src, len, ad.start(mode), from, i, far, m, run, halt);
  if constexpr (kLexBacktrack<Ad>)
    if (out.term != kInvalidSymbol && halt > out.end &&
        halt - out.end > kBacktrackTail &&
        LexSuffixSession<Ad>::shares(ad, mode))
      out = LexSuffixSession<Ad>::finish(ad, src, len, ad.start(mode), from,
                                         halt, out);
  return out;
}

}  // namespace cursiv
