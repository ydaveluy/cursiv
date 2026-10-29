// What the lex modes are built over: one NFA per terminal, and the byte
// classes its edges are written in -- cut once every set is known, since
// resolving a refusal spells sets the grammar never named.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cursiv/engines/shared/gen/HeadMatcher.hpp"
#include "cursiv/engines/shared/gen/LexCheckIncomplete.hpp"
#include "cursiv/engines/shared/gen/LexKeywordCut.hpp"
#include "cursiv/engines/shared/gen/LexNfaBuild.hpp"
#include "cursiv/engines/shared/gen/LexNfaPool.hpp"
#include "cursiv/engines/shared/gen/LexRecursion.hpp"
#include "cursiv/engines/shared/gen/LexRefusalProduct.hpp"
#include "cursiv/engines/shared/gen/LexStrayExpansion.hpp"
#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"
#include "cursiv/engines/shared/gen/bit_words.hpp"
#include "cursiv/engines/shared/gen/lex_boundary_misses.hpp"
#include "cursiv/engines/shared/gen/lex_prefix_meets.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/engines/shared/table/DynSet.hpp"
#include "cursiv/engines/shared/table/TermInfo.hpp"

namespace cursiv {

struct LexAlphabet {
  LexNfaPool nfa;
  LexKeywordCut cut;
  std::vector<int> startOf;       // terminal -> its NFA start, -1 for none
  std::vector<unsigned> repByte;  // class -> one byte that is in it
  unsigned ncls = 0;

  template <class Built, class Intern>
  void terminals(Built& A, Intern internName) {
    auto& g = A.cfg;
    validate_lex_recursion(g);
    if (g.lowerFail()) return;
    startOf.assign(g.nterms, -1);
    bool levels[2] = {};
    for (std::uint16_t t = 1; t < g.nterms; ++t)
      if (const auto v = g.terms[t].laid;
          (v == kLaidIndent || v == kLaidDedent) &&
          std::exchange(levels[v - kLaidIndent], true))
        lower_refuse(g, LowerFail::indentTwice, t);
    LexNfaBuild builder{nfa, g, internName};
    std::vector<LexKeywordCut::Shape> keyword(g.nterms);
    for (std::uint16_t t = 1; t < g.nterms; ++t) {
      if (g.terms[t].laid != 0) continue;
      nfa.term = t;
      const int match = nfa.match(t);
      startOf[t] = builder.terminal(g.termBuild[t].lex, match);
      keyword[t] = LexKeywordCut::shape(g, g.termBuild[t].lex);
    }
    const auto heads = lex_guard_heads(nfa, startOf);
    const auto taken =
        lex_matcher_takes(nfa, g, lex_spine_offsets(nfa, g, startOf));
    A.termHeads = lex_head_words(nfa, g, startOf, taken, A.termTails);
    for (std::uint16_t t = 1; t < g.nterms; ++t)
      g.terms[t].refuses = !A.termHeads[t].words.empty();
    // A head refusal whose rescue leaves out bytes the terminal reads.
    lex_boundary_misses(nfa, g, heads, [&](std::size_t k, const ByteSet& b) {
      if (!g.lowerFail()) g.lowerWord = nfa.guards[k].node;
      lower_refuse(g, LowerFail::boundaryMisses, nfa.guards[k].term, b);
    });
    LexStrayExpansion(nfa, g).run();
    for (const LexNfaState& x : nfa.s) {
      if (x.accept < 0 || x.bound < 0) continue;
      const auto& own = keyword[static_cast<std::size_t>(x.accept)];
      const auto bound = static_cast<std::uint16_t>(x.bound);
      if (own.any && !lex_word_set(g, bound) &&
          (own.all ||
           (g.sets[bound].covers(g.wordBytes) &&
            (g.sets[bound] | lex_decode_bytes(g, bound)).covers(g.wordLeads))))
        g.keywordBounds.v.push_back(bound);
    }
    tied_trivia(g);
    cut = LexKeywordCut::of(g, nfa, startOf);
    classes(A);
  }

  // Whether one text is matched whole by both graphs and read at one rank: a
  // literal's or a keyword's branch outranks a pattern's on the text.
  template <class Cfg>
  LexOverlap text_tied(const Cfg& g, int a, int b) const {
    const auto outranks = [&](const std::vector<int>& sub) {
      return std::ranges::any_of(sub, [&](int x) {
        const LexNfaState& st = nfa.s[static_cast<std::size_t>(x)];
        return st.accept >= 0 &&
               (st.lit ||
                (st.bound >= 0 &&
                 lex_word_set(g, static_cast<std::uint16_t>(st.bound))));
      });
    };
    return lex_pair_search(
        nfa, g.sets.data(), a, b,
        [&](const std::vector<int>& x, const std::vector<int>& y) {
          return lex_accepts(nfa, x) && lex_accepts(nfa, y) &&
                 outranks(x) == outranks(y);
        });
  }

  // Two trivia that read one text at one rank leave the tree to the order
  // they are declared in, unless neither is kept in it. A walk that cannot
  // conclude leaves the pair standing, with a warning.
  template <class Cfg>
  void tied_trivia(Cfg& g) const {
    std::vector<std::vector<std::uint16_t>> skippers;
    if (g.cfgTriviaAt.size() == 0) {
      skippers.emplace_back();
      for (std::uint16_t t = 1; t < g.nterms; ++t)
        if (g.terms[t].trivia) skippers.back().push_back(t);
    }
    for (std::size_t k = 0; k + 1 < g.cfgTriviaAt.size(); ++k) {
      skippers.emplace_back();
      for (std::uint32_t i = g.cfgTriviaAt[k]; i < g.cfgTriviaAt[k + 1]; ++i)
        skippers.back().push_back(static_cast<std::uint16_t>(g.cfgTrivia[i]));
    }
    std::unordered_set<std::uint32_t> apart;
    for (const auto& skipped : skippers)
      for (std::size_t i = 0; i < skipped.size(); ++i)
        for (std::size_t j = i + 1; j < skipped.size(); ++j) {
          const TermInfo& x = g.terms[skipped[i]];
          const TermInfo& y = g.terms[skipped[j]];
          if (!(x.recorded || y.recorded) ||
              !apart.insert(std::uint32_t{skipped[i]} << 16 | skipped[j])
                   .second)
            continue;
          const LexOverlap tie =
              text_tied(g, startOf[skipped[i]], startOf[skipped[j]]);
          if (tie == LexOverlap::found)
            return lower_refuse(g, LowerFail::triviaTied, skipped[j]);
          if (tie == LexOverlap::incomplete)
            lex_check_incomplete(g.lexIncomplete, {LowerFail::triviaTied,
                                                   skipped[i], skipped[j]});
        }
  }

  // A section ends where its closing token stands, which no token of its
  // grammar may read or begin with; and the document's indentation is no
  // section's. `reads(g)` is what each section's grammar reads. A walk that
  // cannot conclude leaves the token standing, with a warning.
  template <class Cfg, class Reads>
  void section_ends(Cfg& g, Reads&& reads) const {
    if (!g.sections()) return;
    for (unsigned t = 1; t < g.nterms; ++t)
      if (g.terms[t].laid == kLaidIndent || g.terms[t].laid == kLaidDedent) {
        lower_refuse(g, LowerFail::sectionIndented);
        return;
      }
    const std::vector<DynSet> own = reads(g);
    for (std::size_t h = 1; h < own.size(); ++h) {
      if (g.cfgHome[h] != h) continue;
      const int end = startOf[g.cfgEnd[h]];
      each_bit(own[h].w.data(), own[h].w.size(), [&](std::size_t t) {
        const LexOverlap meets =
            lex_prefix_meets(nfa, g.sets.data(), end, startOf[t]);
        if (meets == LexOverlap::found)
          lower_refuse(g, LowerFail::sectionEndShared);
        else if (meets == LexOverlap::incomplete)
          lex_check_incomplete(g.lexIncomplete,
                               {LowerFail::sectionEndShared, g.cfgEnd[h],
                                static_cast<std::uint16_t>(t)});
      });
    }
  }

  // The disjoint byte atoms of the table's sets, numbered by their first byte.
  template <class Built>
  void classes(Built& A) {
    const auto& g = A.cfg;
    // A byte's signature names every set it belongs to, one bit each, as
    // wide as the sets are many: a class of code points cuts hundreds.
    const std::size_t words = (g.nsets + 63u) / 64u;
    std::vector<std::vector<std::uint64_t>> sig(
        256, std::vector<std::uint64_t>(words));
    const ByteSet* const bs = g.sets.data();
    for (unsigned i = 0; i < g.nsets; ++i)
      each_bit(bs[i].w, 4, [&](std::size_t b) {
        sig[b][i >> 6] |= std::uint64_t{1} << (i & 63u);
      });
    std::vector<std::vector<std::uint64_t>> seen;
    for (unsigned b = 0; b < 256; ++b) {
      const auto at = std::find(seen.begin(), seen.end(), sig[b]);
      A.cls[b] = static_cast<std::uint8_t>(at - seen.begin());
      if (at == seen.end()) seen.push_back(sig[b]);
    }
    auto n = static_cast<unsigned>(seen.size());
    if (n > 0xffu) {
      A.cap_hit("the 8-bit count of byte classes");
      for (unsigned b = 0; b < 256; ++b)
        if (A.cls[b] > 0xfeu) A.cls[b] = 0xfe;
      n = 0xff;
    }
    A.ncls = static_cast<std::uint8_t>(n);
    ncls = n;
    repByte.assign(ncls, 0u);
    for (unsigned b = 256; b-- > 0;) repByte[A.cls[b]] = b;
  }
};

}  // namespace cursiv
