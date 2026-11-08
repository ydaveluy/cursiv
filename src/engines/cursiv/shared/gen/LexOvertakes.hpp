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
#include <unordered_set>
#include <utility>
#include <vector>

#include "cursiv/shared/gen/LexNfaPool.hpp"
#include "cursiv/shared/gen/bit_words.hpp"
#include "cursiv/shared/gen/lex_intern_set.hpp"
#include "cursiv/shared/gen/lex_prefix_meets.hpp"
#include "cursiv/shared/table/ByteSet.hpp"
#include "cursiv/shared/table/DynSet.hpp"

namespace cursiv::engine {

template <class Cfg>
class LexOvertakes {
 public:
  // `followers` lists, per terminal, the terminals that may follow it.
  LexOvertakes(const LexNfaPool& nfa, const Cfg& g,
               const std::vector<int>& startOf,
               const std::vector<DynSet>& followers)
      : _nfa(nfa), _g(g), _startOf(startOf), _followers(followers),
        _asked(g.nterms), _past(g.nterms), _sweepReads(nfa.s.size()),
        _next(g.nterms), _nextKnown(g.nterms, false), _tied(g.nterms, 0) {
    for (unsigned t = 1; t < g.nterms; ++t)
      if (g.terms[t].trivia) _trivia.set(t);
  }

  // The terminals of `added` that, offered beside those of `kept` and the
  // trivia, read over one of them, or over a trivia whose text they match, as
  // the lexer elects the token: a caller that does not take the token reads
  // that text as trivia, and the position past it is another's to answer.
  DynSet overtaking(const DynSet& added, const DynSet& kept) {
    DynSet fresh = added;
    fresh.andnot(kept);
    return over(fresh, kept);
  }

  // Those of `candidates` that read over one of `beside` or over a trivia.
  DynSet over(const DynSet& candidates, const DynSet& beside) {
    DynSet out;
    each_bit(candidates.w.data(), candidates.w.size(), [&](std::size_t a) {
      if (a >= _g.nterms || _trivia.test(static_cast<unsigned>(a))) return;
      if (reads_over(static_cast<unsigned>(a), beside) ||
          takes_trivia(static_cast<unsigned>(a)))
        out.set(static_cast<unsigned>(a));
    });
    return out;
  }

  // Whether `a` runs past one of `beside`, `a` itself aside: the answers known
  // first, a word at a time, then the terminals not asked yet that `a` stands
  // on where they end. Trivia count where `beside` names them.
  bool reads_over(unsigned a, const DynSet& beside) {
    sweep_on();
    if (_swept && !_open.test(a)) {
      for (const std::uint16_t b : _meets[a])
        if (beside.test(b) &&
            (_asked[a].test(b) ? _past[a].test(b) : runs_past(b, a)))
          return true;
      return false;
    }
    if (_past[a].intersects(beside)) return true;
    _ask = beside;
    _ask.andnot(_asked[a]);
    bool over = false;
    each_bit(_ask.w.data(), _ask.w.size(), [&](std::size_t b) {
      over = over || (b < _g.nterms && runs_past(static_cast<unsigned>(b), a));
    });
    return over;
  }

  // The pairs walked, the graph states their walks stepped, and those the sweep
  // did.
  std::uint64_t walks() const { return _walks; }
  std::uint64_t walk_reads() const { return _walkReads; }
  std::uint64_t sweep_reads() const { return _sweepReads; }

  // Ends the sweep now, whatever the walks have read.
  void finish_sweep() {
    while (!_sweepOver) sweep_step();
  }

  // Whether `a` reads over a trivia, one of the same text included.
  bool takes_trivia(unsigned a) {
    if (_tied[a] == 0) {
      bool over = false;
      if (_startOf[a] >= 0)
        each_bit(_trivia.w.data(), _trivia.w.size(), [&](std::size_t b) {
          over = over || (b < _g.nterms && _startOf[b] >= 0 &&
                          walk(static_cast<unsigned>(b), a, true));
        });
      _tied[a] = over ? 2 : 1;
    }
    return _tied[a] == 2;
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
  // Per terminal `a`, the terminals that end on a text `a` stands on, ascending,
  // and those whose relays leave what they read to a walk of their own.
  std::vector<std::vector<std::uint16_t>> _meets;
  DynSet _open;
  bool _swept = false, _sweepOver = false;
  // What the sweep has yet to read, and what it has found.
  using Item = std::uint64_t;  // terminal << 32 | state, ascending
  std::vector<DynSet> _sweepMeets;
  std::vector<std::vector<Item>> _sweepWork;
  std::unordered_set<std::vector<Item>, VecHash> _sweepSeen;
  std::size_t _sweepAt = 0, _sweepItems = 0;
  std::uint64_t _walks = 0;
  // The graph states stepped; the sweep's opening is charged before it is read.
  std::uint64_t _walkReads = 0, _sweepReads = 0;
  // Per terminal, where a token that may follow it starts, once asked.
  std::vector<std::vector<int>> _next;
  std::vector<bool> _nextKnown;
  DynSet _trivia;
  // Per terminal, whether it reads over a trivia, one of the same text
  // included: 0 not asked yet, 1 no, 2 yes.
  std::vector<std::uint8_t> _tied;

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

  // Reads every terminal's graph at once, each subset of them once: `a` can
  // only run past a text `b` ends on where the two stand together. A pair this
  // rules out is one a walk finds nothing for: it steps the same subsets, and
  // has as many pairs at most as there are subsets here, short of its budget.
  //
  // Whether it pays depends on the walks still to come, which nothing says, and
  // on how far its subsets multiply. So the walks pay for it: it reads as many
  // states as they have read, and waits where it stands until they read more. A
  // grammar pays at most that, however its subsets multiply, and one that goes
  // on asking has the sweep once the walks have read its size. Past the limits
  // below, every pair is left to its walk.
  void sweep_on() {
    while (!_sweepOver && _sweepReads < _walkReads) sweep_step();
  }

  void sweep_open() {
    const std::size_t nterms = _g.nterms;
    _sweepMeets.resize(nterms);
    // A terminal that holds a relay reads what no subset says.
    std::vector<unsigned> held(_nfa.s.size(), 0);
    std::vector<int> todo;
    std::vector<Item> root;
    for (unsigned t = 1; t < nterms; ++t) {
      if (_startOf[t] < 0) continue;
      bool relays = false;
      todo.assign(1, _startOf[t]);
      while (!todo.empty()) {
        const int at = todo.back();
        todo.pop_back();
        if (std::exchange(held[static_cast<std::size_t>(at)], t) == t) continue;
        const LexNfaState& st = _nfa.s[static_cast<std::size_t>(at)];
        relays = relays || st.relay >= 0;
        if (st.t1 >= 0) todo.push_back(st.t1);
        if (st.t2 >= 0) todo.push_back(st.t2);
      }
      if (relays) {
        _open.set(t);
        continue;
      }
      for (const int x : sorted_closure(std::span<const int>(&_startOf[t], 1)))
        root.push_back(std::uint64_t{t} << 32 | static_cast<std::uint32_t>(x));
    }
    _sweepSeen.insert(root);
    _sweepWork.push_back(std::move(root));
  }

  void sweep_end() {
    _sweepOver = true;
    _sweepWork = {};
    _sweepSeen = {};
    _sweepMeets = {};
  }

  // Reads one subset, the first of them after the sweep's opening.
  void sweep_step() {
    constexpr std::size_t kMostSubsets = 20000, kMostItems = std::size_t{1} << 22;
    if (_sweepWork.empty()) sweep_open();
    if (_sweepAt == _sweepWork.size()) {
      _meets.resize(_g.nterms);
      for (std::size_t a = 0; a < _g.nterms; ++a) {
        _sweepMeets[a] |= _open;
        each_bit(_sweepMeets[a].w.data(), _sweepMeets[a].w.size(),
                 [&](std::size_t b) {
                   _meets[a].push_back(static_cast<std::uint16_t>(b));
                 });
      }
      _swept = true;
      sweep_end();
      return;
    }
    if (_sweepSeen.size() > kMostSubsets || _sweepItems > kMostItems) {
      sweep_end();
      return;
    }
    const std::vector<Item> at = std::move(_sweepWork[_sweepAt++]);
    _sweepReads += at.size();
    const ByteSet* const sets = _g.sets.data();
    std::vector<ByteSet> reads(at.size());
    std::vector<int> leads;
    std::vector<unsigned> ending, standing;
    ByteSet left;
    for (std::size_t k = 0; k < at.size(); ++k) {
      const auto t = static_cast<unsigned>(at[k] >> 32);
      const LexNfaState& st = _nfa.s[static_cast<std::uint32_t>(at[k])];
      if (standing.empty() || standing.back() != t) standing.push_back(t);
      if (st.accept >= 0 && (ending.empty() || ending.back() != t))
        ending.push_back(t);
      if (st.set >= 0) {
        reads[k] = sets[st.set];
        if (st.minus >= 0) reads[k].andnot(sets[st.minus]);
      }
      left |= reads[k];
    }
    _sweepReads += ending.size() * standing.size();
    for (const unsigned b : ending)
      for (const unsigned a : standing) _sweepMeets[a].set(b);
    // Bytes every edge of the subset reads alike step it alike.
    for (unsigned c = 0; c < 256; ++c) {
      const auto byte = static_cast<unsigned char>(c);
      if (!left.has(byte)) continue;
      _sweepReads += at.size();
      ByteSet alike = left;
      for (const ByteSet& on : reads)
        if (on.has(byte))
          alike &= on;
        else
          alike.andnot(on);
      left.andnot(alike);
      std::vector<Item> next;
      for (std::size_t k = 0; k < at.size();) {
        const std::uint64_t t = at[k] >> 32;
        leads.clear();
        for (; k < at.size() && at[k] >> 32 == t; ++k)
          if (reads[k].has(byte))
            leads.push_back(_nfa.s[static_cast<std::uint32_t>(at[k])].t1);
        if (leads.empty()) continue;
        for (const int x : sorted_closure(leads))
          next.push_back(t << 32 | static_cast<std::uint32_t>(x));
      }
      if (!next.empty() && _sweepSeen.insert(next).second) {
        _sweepItems += next.size();
        _sweepWork.push_back(std::move(next));
      }
    }
  }

  std::vector<int> sorted_closure(std::span<const int> roots) const {
    std::vector<int> out = _nfa.eclose(roots);
    std::ranges::sort(out);
    return out;
  }

  // Where a token that may follow `b` starts, a trivia included.
  const std::vector<int>& after(unsigned b) {
    if (!_nextKnown[b]) {
      _nextKnown[b] = true;
      std::vector<int> starts;
      const DynSet next = _followers[b] | _trivia;
      each_bit(next.w.data(), next.w.size(), [&](std::size_t u) {
        if (u != 0 && u < _g.nterms && _startOf[u] >= 0)
          starts.push_back(_startOf[u]);
      });
      _next[b] = _nfa.eclose(std::span<const int>(starts));
      std::ranges::sort(_next[b]);
    }
    return _next[b];
  }

  // Whether `a` reads on past a text `b` ends on, into the start of a token
  // that may follow `b`: that token is lost to the match of `a`.
  bool runs_past(unsigned b, unsigned a) {
    _asked[a].set(b);
    if (b == a || _startOf[a] < 0 || _startOf[b] < 0) return false;
    ++_walks;
    if (!walk(b, a)) return false;
    _past[a].set(b);
    return true;
  }

  // With `tied`, `a` also reads over `b`, a trivia, where both end on one
  // text.
  bool walk(unsigned b, unsigned a, bool tied = false) {
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
      _walkReads += ra.size() + rx.size();
      if (phase == 0 && lex_accepts(_nfa, rx)) {
        // A recognizer reads on as far as its machine goes.
        if (lex_recognizes(_nfa, ra) || (tied && lex_accepts(_nfa, ra)))
          return true;
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
        _walkReads += ra.size() + rx.size();
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

}  // namespace cursiv::engine
