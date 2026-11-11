// A keyword's or a literal's spelling, reserved where a DFA state accepts it:
// the pattern terminals the state also accepts lose that spelling. A longer
// run re-admits them one state further, so `kwx` stays a word.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "cursiv/shared/gen/lex_intern_set.hpp"
#include "cursiv/shared/table/ByteSet.hpp"
#include "cursiv/shared/table/DynSet.hpp"
#include "cursiv/shared/table/LexRelay.hpp"
#include "cursiv/shared/table/TermInfo.hpp"
#include "cursiv/shared/table/Token.hpp"

namespace cursiv::engine {

// The terminals a relay launched at `s`, or at a state that reaches `s`,
// defers. `launched(s, each)` names each relay's terminal, and
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

// Reserves each state's keywords and literals with `reserve(s, term)`, from
// the terminals that read them as patterns or that a relay defers; trivia
// reserves nothing. Returns the keywords each state accepts.
template <class Cfg, class View, class Relays, class Accepts, class Bounds,
          class Reserve, class Held>
LexReserved lex_reserve(const Cfg& g, const View& v, std::size_t ndfa,
                        const Relays& relays, Accepts&& accepts,
                        Bounds&& bounds, Reserve&& reserve, Held&& held) {
  // The terminals relays defer, unprioritized ones with what the state accepts,
  // and the prioritized ones pending.
  std::vector<DynSet> deferred, pending;
  if (!relays.empty()) {
    const auto successors = [&](std::size_t s, auto&& each) {
      for (unsigned c = 0; c < v.classes(); ++c)
        if (const auto to = v.next(static_cast<std::uint16_t>(s), c);
            to != kDeadState)
          each(to);
    };
    const auto launched = [&](bool prioritized) {
      return [&, prioritized](std::size_t s, auto&& each) {
        for (auto k = v.relay(static_cast<std::uint16_t>(s)); k != kNoRelay;
             k = relays[k].next)
          if (!relays[k].gate && (relays[k].priority != 0) == prioritized)
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
    bounds(s, [&](unsigned t, std::uint16_t bound, unsigned priority) {
      const bool word = bound != kNoSet && lex_word_set(g, bound);
      if (bound == kNoSet) open.set(t);
      if (word)
        words.set(t);
      else if (priority == kLiteralPriority && !g.terms[t].trivia)
        literals.set(t);
      reserving = reserving || ((word || priority == kLiteralPriority) &&
                                !g.terms[t].trivia);
    });
    out.keywords[s] = words;
    out.keywords[s].andnot(open);
    out.standing[s] = words.intersects(open);
    bool readers = false;
    if (s < pending.size())
      for (const unsigned t : pending[s].members())
        readers = readers || t == 0 || t >= g.nterms || !g.terms[t].trivia;
    if (!reserving && !readers) continue;
    const DynSet& acc = accepted(s);
    for (const unsigned u : acc.members()) {
      if (u == 0 || g.terms[u].spelled || words.test(u) || literals.test(u))
        continue;
      if (reserving) reserve(s, u);
      if (readers && !pending[s].test(u)) held(s, u);
    }
  }
  return out;
}

}  // namespace cursiv::engine
