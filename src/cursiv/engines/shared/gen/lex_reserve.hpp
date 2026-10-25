// A keyword's or a literal's spelling, reserved where a DFA state accepts it:
// behind a boundary of word bytes a terminal is a keyword, and the terminals
// the state also accepts as patterns, or a relay on the way to it defers, lose
// that spelling. A longer run re-admits them one state further, so `kwx` stays
// a word.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "cursiv/engines/shared/gen/LexCodePoints.hpp"
#include "cursiv/engines/shared/gen/bit_words.hpp"
#include "cursiv/engines/shared/gen/lex_intern_set.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/engines/shared/table/DynSet.hpp"
#include "cursiv/engines/shared/table/TermInfo.hpp"

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
// keyword alone, by a keyword beside an unbounded reading, and by a literal.
struct LexReserved {
  std::vector<DynSet> keywords;
  std::vector<DynSet> standing;
  std::vector<DynSet> literals;
};

// `accepts(s)` is what state `s` accepts and relays defer; `bounds(s, each)`
// hands `each(term, bound, tier)` every acceptance of `s` with its boundary
// and tier; `reserve(s, term)` takes the spelling from a terminal. A
// keyword's spelling and a literal's are reserved from the terminals that read
// it as a pattern; a terminal whose own body is a keyword or a literal keeps
// it, or its language would empty where it is accepted. Trivia reserves
// nothing: at one length a token beats it. Answers the keywords each state
// accepts, the terminals that also read there with no boundary -- a word byte
// after the keyword does not take those away -- and its literals.
template <class Cfg, class Accepts, class Bounds, class Reserve>
LexReserved lex_reserve(const Cfg& g, std::size_t ndfa, Accepts&& accepts,
                        Bounds&& bounds, Reserve&& reserve) {
  const ByteSet leads = lex_leads_of(g.wordRanges.data(), g.wordRanges.size());
  LexReserved out;
  out.keywords.assign(ndfa, DynSet{});
  out.standing.assign(ndfa, DynSet{});
  out.literals.assign(ndfa, DynSet{});
  for (std::size_t s = 0; s < ndfa; ++s) {
    DynSet words, open;
    bool reserving = false;
    bounds(s, [&](unsigned t, std::uint16_t bound, unsigned tier) {
      const bool word = bound != kNoSet && lex_word_set(g, bound, leads);
      if (bound == kNoSet) open.set(t);
      if (word)
        words.set(t);
      else if (tier == kTierLiteral && !g.terms[t].trivia)
        out.literals[s].set(t);
      reserving =
          reserving || ((word || tier == kTierLiteral) && !g.terms[t].trivia);
    });
    out.keywords[s] = words;
    out.keywords[s].andnot(open);
    out.standing[s] = words;
    out.standing[s] &= open;
    if (!reserving) continue;
    const DynSet& acc = accepts(s);
    for (unsigned u = 1; u < g.nterms; ++u)
      if (acc.test(u) && !g.terms[u].spelled && !words.test(u) &&
          !out.literals[s].test(u))
        reserve(s, u);
  }
  return out;
}

// What a literal or a keyword read behind conditions takes, where one may
// end: `pending(s)` holds such readings. The terminals of `accepts(s)` read as
// patterns lose their spelling only if the reading stands, so these are
// reserved apart from the ones a state's own readings reserve.
template <class Cfg, class Accepts, class Reserve>
void lex_reserve_pending(const Cfg& g, std::size_t ndfa,
                         const LexReserved& reserved,
                         const std::vector<DynSet>& pending, Accepts&& accepts,
                         Reserve&& reserve) {
  for (std::size_t s = 0; s < ndfa; ++s) {
    bool readers = false;
    each_bit(pending[s].w.data(), pending[s].w.size(), [&](std::size_t t) {
      readers = readers || t == 0 || t >= g.nterms || !g.terms[t].trivia;
    });
    if (!readers) continue;
    const DynSet& acc = accepts(s);
    for (unsigned u = 1; u < g.nterms; ++u)
      if (acc.test(u) && !g.terms[u].spelled && !reserved.keywords[s].test(u) &&
          !reserved.standing[s].test(u) && !reserved.literals[s].test(u) &&
          !pending[s].test(u))
        reserve(s, u);
  }
}

}  // namespace cursiv
