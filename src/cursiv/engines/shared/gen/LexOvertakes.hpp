// What offering a terminal beside others does to the lexer: it reads over a
// terminal when its match runs past the one the other ends on, into a token
// that may follow it -- the longest match takes the text the reading that goes
// on would have read.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <set>
#include <span>
#include <tuple>
#include <vector>

#include "cursiv/engines/shared/gen/LexNfaPool.hpp"
#include "cursiv/engines/shared/gen/bit_words.hpp"
#include "cursiv/engines/shared/gen/lex_intern_set.hpp"
#include "cursiv/engines/shared/gen/lex_prefix_meets.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/engines/shared/table/DynSet.hpp"

namespace cursiv {

template <class Cfg>
class LexOvertakes {
 public:
  // `followers` lists, per terminal, the terminals that may follow it.
  LexOvertakes(const LexNfaPool& nfa, const Cfg& g,
               const std::vector<int>& startOf,
               const std::vector<DynSet>& followers)
      : _nfa(nfa), _g(g), _startOf(startOf), _followers(followers),
        _asked(g.nterms), _past(g.nterms), _next(g.nterms),
        _nextKnown(g.nterms, false) {
    for (unsigned t = 1; t < g.nterms; ++t)
      if (g.terms[t].trivia) _trivia.set(t);
  }

  // The terminals of `added` that, offered beside those of `kept` and the
  // trivia, read over one of them.
  DynSet overtaking(const DynSet& added, const DynSet& kept) {
    DynSet fresh = added;
    fresh.andnot(kept);
    const DynSet beside = kept | _trivia;
    DynSet out;
    each_bit(fresh.w.data(), fresh.w.size(), [&](std::size_t a) {
      if (a >= _g.nterms || _trivia.test(static_cast<unsigned>(a))) return;
      if (reads_over(static_cast<unsigned>(a), beside))
        out.set(static_cast<unsigned>(a));
    });
    return out;
  }

 private:
  const LexNfaPool& _nfa;
  const Cfg& _g;
  const std::vector<int>& _startOf;
  const std::vector<DynSet>& _followers;
  // Per terminal `a`, the terminals asked whether `a` runs past them, and
  // those it does.
  std::vector<DynSet> _asked, _past;
  // The terminals `reads_over` has still to ask, kept to reuse its words.
  DynSet _ask;
  // Per terminal, where a token that may follow it starts, once asked.
  std::vector<std::vector<int>> _next;
  std::vector<bool> _nextKnown;
  DynSet _trivia;

  // The bytes after which a match of the subset stands.
  ByteSet stands_before(const std::vector<int>& sub) const {
    ByteSet out;
    for (const int x : sub) {
      const LexNfaState& st = _nfa.s[static_cast<std::size_t>(x)];
      if (st.accept < 0) continue;
      if (st.bound < 0) return lex_not(ByteSet{});
      out |= lex_not(_g.sets[static_cast<std::size_t>(st.bound)]);
    }
    return out;
  }

  // Where a token that may follow `b` starts, a trivia included.
  const std::vector<int>& after(unsigned b) {
    if (!_nextKnown[b]) {
      _nextKnown[b] = true;
      std::vector<int> starts;
      for (unsigned u = 1; u < _g.nterms; ++u)
        if (_startOf[u] >= 0 && (_followers[b].test(u) || _trivia.test(u)))
          starts.push_back(_startOf[u]);
      _next[b] = _nfa.eclose(std::span<const int>(starts));
      std::ranges::sort(_next[b]);
    }
    return _next[b];
  }

  // Whether `a` runs past one of `beside`: the answers known first, a word at
  // a time, then the terminals not asked yet.
  bool reads_over(unsigned a, const DynSet& beside) {
    if (_past[a].intersects(beside)) return true;
    _ask = beside;
    _ask.andnot(_asked[a]);
    bool over = false;
    each_bit(_ask.w.data(), _ask.w.size(), [&](std::size_t b) {
      over = over || (b < _g.nterms && runs_past(static_cast<unsigned>(b), a));
    });
    return over;
  }

  // Whether `a` reads on past a text `b` ends on, into the start of a token
  // that may follow `b`: that token is lost to the match of `a`.
  bool runs_past(unsigned b, unsigned a) {
    _asked[a].set(b);
    if (b == a || _startOf[a] < 0 || _startOf[b] < 0 || !walk(b, a))
      return false;
    _past[a].set(b);
    return true;
  }

  bool walk(unsigned b, unsigned a) {
    constexpr std::size_t kMost = 20000;
    const std::vector<int>& next = after(b);
    if (next.empty()) return false;
    const ByteSet* const sets = _g.sets.data();
    // `a` beside `b` (phase 0), or beside the tokens that may follow it
    // (phases 1 and 2), the bytes the first step past `b` may take in the
    // first of these.
    using Key = std::tuple<std::vector<int>, std::vector<int>, int,
                           std::array<std::uint64_t, 4>>;
    const auto words = [](const ByteSet& s) {
      return std::array<std::uint64_t, 4>{s.w[0], s.w[1], s.w[2], s.w[3]};
    };
    std::set<Key> seen;
    std::vector<Key> work;
    // Past `kMost` pairs, or more assertions at one position than the walk
    // enters, `a` is taken to read over.
    bool unsure = false;
    const auto push = [&](Key k) {
      if (seen.insert(k).second) work.push_back(std::move(k));
    };
    // Each pair the two walks may stand in, whether or not each assertion on
    // the way holds.
    const auto enter = [&](std::vector<int> ra, std::vector<int> rx, int phase,
                           const ByteSet& allowed) {
      const auto xs = lex_assertion_worlds(_nfa, std::move(rx), unsure);
      for (const std::vector<int>& wa :
           lex_assertion_worlds(_nfa, std::move(ra), unsure))
        for (const std::vector<int>& wx : xs)
          push({wa, wx, phase, words(allowed)});
    };
    std::vector<int> first = _nfa.eclose(_startOf[a]);
    std::vector<int> second = _nfa.eclose(_startOf[b]);
    std::ranges::sort(first);
    std::ranges::sort(second);
    enter(std::move(first), std::move(second), 0, ByteSet{});
    for (std::size_t i = 0; i < work.size(); ++i) {
      if (unsure || seen.size() > kMost) return true;
      const auto [ra, rx, phase, allowed] = work[i];
      if (phase == 0 && lex_accepts(_nfa, rx)) {
        // A recognizer reads on as far as its machine goes.
        if (lex_recognizes(_nfa, ra)) return true;
        // `b` ends where its own graph cannot read on.
        ByteSet ends = stands_before(rx);
        ends.andnot(lex_subset_reads(_nfa, sets, rx));
        enter(ra, next, 1, ends);
      }
      ByteSet left =
          lex_subset_reads(_nfa, sets, ra) & lex_subset_reads(_nfa, sets, rx);
      if (phase == 1)
        left &= ByteSet{{allowed[0], allowed[1], allowed[2], allowed[3]}};
      for (unsigned c = 0; c < 256; ++c) {
        const auto byte = static_cast<unsigned char>(c);
        if (!left.has(byte)) continue;
        // Every edge of both subsets reads the bytes of `alike` as it reads
        // `byte`: they step where it steps, and are taken with it.
        ByteSet alike = left;
        for (const std::vector<int>* sub : {&ra, &rx})
          for (const int x : *sub) {
            const LexNfaState& st = _nfa.s[static_cast<std::size_t>(x)];
            if (st.set < 0) continue;
            ByteSet on = sets[st.set];
            if (st.minus >= 0) on.andnot(sets[st.minus]);
            if (on.has(byte))
              alike &= on;
            else
              alike.andnot(on);
          }
        left.andnot(alike);
        auto na = lex_nfa_step(_nfa, sets, ra, byte);
        auto nx = lex_nfa_step(_nfa, sets, rx, byte);
        if (na.empty() || nx.empty()) continue;
        if (phase == 0) {
          enter(std::move(na), std::move(nx), 0, ByteSet{});
          continue;
        }
        const auto xs = lex_assertion_worlds(_nfa, std::move(nx), unsure);
        for (const std::vector<int>& wa :
             lex_assertion_worlds(_nfa, std::move(na), unsure))
          for (const std::vector<int>& wx : xs) {
            // `a` ends in the next token's text, or the next token ends where
            // `a` reads on past it.
            bool tokenEnds = false, triviaEnds = false;
            for (const int x : wx) {
              const LexNfaState& st = _nfa.s[static_cast<std::size_t>(x)];
              if (st.accept >= 0)
                (_trivia.test(static_cast<unsigned>(st.accept)) ? triviaEnds
                                                                : tokenEnds) =
                    true;
            }
            if (lex_accepts(_nfa, wa) || lex_recognizes(_nfa, wa) ||
                lex_recognizes(_nfa, wx) ||
                (tokenEnds && lex_subset_reads(_nfa, sets, wa)
                                  .andnot(lex_subset_reads(_nfa, sets, wx))
                                  .any()))
              return true;
            if (!triviaEnds) {
              push({wa, wx, 2, words(ByteSet{})});
              continue;
            }
            std::vector<int> joined;
            joined.reserve(wx.size() + next.size());
            std::ranges::set_union(wx, next, std::back_inserter(joined));
            enter(wa, std::move(joined), 2, ByteSet{});
          }
      }
    }
    return unsure;
  }
};

}  // namespace cursiv
