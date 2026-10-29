// A keyword's or a literal's spelling, reserved where a DFA state accepts it:
// behind a boundary of word bytes a terminal is a keyword, and the terminals
// the state also accepts as patterns, or a relay on the way to it defers, lose
// that spelling. A longer run re-admits them one state further, so `kwx` stays
// a word.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "cursiv/engines/shared/gen/bit_words.hpp"
#include "cursiv/engines/shared/gen/lex_intern_set.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/engines/shared/table/DynSet.hpp"
#include "cursiv/engines/shared/table/LexRelay.hpp"
#include "cursiv/engines/shared/table/TermInfo.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

// The terminals a relay launched at `s`, or at a state a walk reaches `s` from,
// defers: their readings end where no state of the automaton accepts them.
// `launched(s, each)` hands `each` the terminal of every relay launched at `s`,
// `successors(s, each)` the states its rows lead to.
template <class Launched, class Successors>
std::vector<DynSet> lex_relayed(std::size_t ndfa, Launched&& launched,
                                Successors&& successors) {
  std::vector<DynSet> reach(ndfa);
  std::vector<std::uint32_t> todo;
  for (std::size_t s = 0; s < ndfa; ++s) {
    launched(s, [&](unsigned t) { reach[s].set(t); });
    if (reach[s].any()) todo.push_back(static_cast<std::uint32_t>(s));
  }
  while (!todo.empty()) {
    const std::uint32_t s = todo.back();
    todo.pop_back();
    successors(s, [&](std::uint32_t to) {
      if (reach[to].add(reach[s])) todo.push_back(to);
    });
  }
  return reach;
}

// What a state reserves from: per state, the terminals read there by a
// keyword alone, and whether one is read by a keyword beside an unbounded
// reading.
struct LexReserved {
  std::vector<DynSet> keywords;
  std::vector<bool> standing;
};

// `v` names the automaton (`classes`, `next`, `relay`) and `relays` its relay
// rows; `accepts(s)` is what state `s` accepts; `bounds(s, each)` hands
// `each(term, bound, tier)` every acceptance of `s` with its boundary and
// tier; `reserve(s, term)` takes the spelling from a terminal. A keyword's
// spelling and a literal's are reserved from the terminals that read it as a
// pattern, or that a relay on the way to the state defers; a terminal whose
// own body is a keyword or a literal keeps it, or its language would empty
// where it is accepted. Trivia reserves nothing: at one length a token beats
// it. Answers the keywords each state accepts, and whether a terminal also
// reads one there with no boundary -- a word byte after the keyword does not
// take that reading away.
//
// What a literal or a keyword read behind conditions takes, where one may
// end, `held(s, term)` takes apart: the terminals read as patterns lose their
// spelling only if the reading stands.
template <class Cfg, class View, class Relays, class Accepts, class Bounds,
          class Reserve, class Held>
LexReserved lex_reserve(const Cfg& g, const View& v, std::size_t ndfa,
                        const Relays& relays, Accepts&& accepts,
                        Bounds&& bounds, Reserve&& reserve, Held&& held) {
  // The terminals relays defer, untiered ones with what the state accepts,
  // and the tiered ones pending.
  std::vector<DynSet> deferred, pending;
  if (!relays.empty()) {
    const auto successors = [&](std::size_t s, auto&& each) {
      for (unsigned c = 0; c < v.classes(); ++c)
        if (const auto to = v.next(static_cast<std::uint16_t>(s), c);
            to != kDeadState)
          each(to);
    };
    const auto launched = [&](bool tiered) {
      return [&, tiered](std::size_t s, auto&& each) {
        for (auto k = v.relay(static_cast<std::uint16_t>(s)); k != kNoRelay;
             k = relays[k].next)
          if (!relays[k].gate && (relays[k].tier != 0) == tiered)
            each(relays[k].term);
      };
    };
    deferred = lex_relayed(ndfa, launched(false), successors);
    pending = lex_relayed(ndfa, launched(true), successors);
    for (std::size_t s = 0; s < ndfa; ++s) deferred[s] |= accepts(s);
  }
  const auto accepted = [&](std::size_t s) -> const DynSet& {
    return deferred.empty() ? accepts(s) : deferred[s];
  };
  LexReserved out;
  out.keywords.assign(ndfa, DynSet{});
  out.standing.assign(ndfa, false);
  for (std::size_t s = 0; s < ndfa; ++s) {
    DynSet words, open, literals;
    bool reserving = false;
    bounds(s, [&](unsigned t, std::uint16_t bound, unsigned tier) {
      const bool word = bound != kNoSet && lex_word_set(g, bound);
      if (bound == kNoSet) open.set(t);
      if (word)
        words.set(t);
      else if (tier == kTierLiteral && !g.terms[t].trivia)
        literals.set(t);
      reserving =
          reserving || ((word || tier == kTierLiteral) && !g.terms[t].trivia);
    });
    out.keywords[s] = words;
    out.keywords[s].andnot(open);
    out.standing[s] = words.intersects(open);
    bool readers = false;
    if (s < pending.size())
      each_bit(pending[s].w.data(), pending[s].w.size(), [&](std::size_t t) {
        readers = readers || t == 0 || t >= g.nterms || !g.terms[t].trivia;
      });
    if (!reserving && !readers) continue;
    const DynSet& acc = accepted(s);
    for (unsigned u = 1; u < g.nterms; ++u) {
      if (!acc.test(u) || g.terms[u].spelled || words.test(u) ||
          literals.test(u))
        continue;
      if (reserving) reserve(s, u);
      if (readers && !pending[s].test(u)) held(s, u);
    }
  }
  return out;
}

}  // namespace cursiv
