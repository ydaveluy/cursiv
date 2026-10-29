// The lex modes of a table: what each one lexes, what each configuration
// skips, and the terminals every mode crosses.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "cursiv/engines/shared/gen/LexRecursion.hpp"
#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"
#include "cursiv/engines/shared/table/DynSet.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

struct LexModes {
  std::vector<DynSet> set;
  // Per mode, the terminals it lexes only for the length of the token, and the
  // mode that lexes them: a token of the mode is the longer of its own and
  // theirs, its own at equal length. `kNoMode` where there are none.
  std::vector<DynSet> longer;
  std::vector<std::uint16_t> longerMode;
  // A mode is keyed by its tag too: modes of different tags never merge.
  std::vector<std::uint32_t> tag;
  DynSet trivia;
  // The line terminal of a bracketed grammar whose trivia read no line ending:
  // blank inside brackets, so every mode lexes it.
  DynSet joins;
  std::vector<DynSet> skips;

  // A program laid by hand names only the grammar's trivia.
  template <class Cfg>
  explicit LexModes(const Cfg& g)
      : skips(std::max<std::size_t>(g.cfgTriviaAt.size(), 2) - 1) {
    bool bracketed = false, linesCrossed = false;
    for (unsigned t = 1; t < g.nterms; ++t) {
      bracketed = bracketed || g.terms[t].delim != 0;
      if (!g.terms[t].trivia) continue;
      trivia.set(t);
      if (g.termBuild[t].lex >= g.nlex) continue;
      const ByteSet first = lex_first_bytes(g, g.termBuild[t].lex).first;
      linesCrossed = linesCrossed || first.has('\n') || first.has('\r');
    }
    for (unsigned t = 1; bracketed && !linesCrossed && t < g.nterms; ++t)
      if (g.terms[t].joins) joins.set(t);
    if (g.cfgTriviaAt.size() == 0) skips[0] = trivia;
    for (std::size_t k = 0; k + 1 < g.cfgTriviaAt.size(); ++k)
      for (std::uint32_t i = g.cfgTriviaAt[k]; i < g.cfgTriviaAt[k + 1]; ++i)
        skips[k].set(g.cfgTrivia[i]);
  }

  std::uint16_t intern(const DynSet& want, std::uint32_t at = 0,
                       const DynSet& beyond = DynSet{}) {
    for (std::size_t i = 0; i < set.size(); ++i)
      if (tag[i] == at && set[i] == want && longer[i] == beyond)
        return static_cast<std::uint16_t>(i);
    if (set.size() >= UINT16_MAX)
      throw LowerRefused(LowerFail::tokenGraphTooLarge);
    const std::uint16_t reach = beyond.any() ? intern(beyond) : kNoMode;
    if (set.size() >= UINT16_MAX)
      throw LowerRefused(LowerFail::tokenGraphTooLarge);
    set.push_back(want);
    longer.push_back(beyond);
    longerMode.push_back(reach);
    tag.push_back(at);
    return static_cast<std::uint16_t>(set.size() - 1);
  }

  // Whether mode `a` lexes everything mode `b` does, to the same length.
  bool covers(std::size_t a, std::size_t b) const {
    return set[a].covers(set[b]) && longer[a].covers(longer[b]);
  }

  // The mode that lexes every terminal, so a repair can name a token the
  // parse could not read.
  template <class Cfg>
  std::uint16_t error_mode(const Cfg& g) {
    DynSet all;
    for (unsigned t = 1; t < g.nterms; ++t)
      if (!g.termBuild[t].predicateOnly) all.set(t);
    return intern(all);
  }

  // Per section configuration, the mode a fault in it lexes: its grammar's
  // terminals `own`, its trivia and the token that closes it, so a fault is
  // never read as a token of the caller's. Outside any section, `err`.
  template <class Cfg>
  std::vector<std::uint16_t> section_modes(const Cfg& g,
                                           std::vector<DynSet> own,
                                           std::uint16_t err) {
    std::vector<std::uint16_t> out(own.size(), err);
    for (std::size_t k = 0; k < own.size(); ++k)
      for (std::uint32_t i = g.cfgTriviaAt[k]; i < g.cfgTriviaAt[k + 1]; ++i)
        own[g.cfgHome[k]].set(g.cfgTrivia[i]);
    for (std::size_t h = 1; h < own.size(); ++h)
      if (g.cfgHome[h] == h) {
        own[h].set(g.cfgEnd[h]);
        out[h] = intern(own[h]);
      }
    return out;
  }

  // Per mode, the terminals it lexes that refuse words behind their head.
  template <class Cfg>
  std::vector<DynSet> heads(const Cfg& g) const {
    DynSet refusing;
    for (unsigned t = 1; t < g.nterms; ++t)
      if (g.terms[t].refuses) refusing.set(t);
    std::vector<DynSet> out;
    for (const DynSet& modeSet : set) out.push_back(modeSet & refusing);
    return out;
  }

  // `mark(a, b)` for each mode `a` that admits everything `b` does, and with
  // `sameTrivia`, the same trivia.
  template <class Mark>
  void cover(bool sameTrivia, Mark&& mark) const {
    std::vector<DynSet> blank;
    if (sameTrivia)
      for (const DynSet& s : set) blank.push_back(s & trivia);
    for (std::size_t a = 0; a < set.size(); ++a)
      for (std::size_t b = 0; b < set.size(); ++b)
        if (covers(a, b) && (!sameTrivia || blank[a] == blank[b])) mark(a, b);
  }
};

}  // namespace cursiv
