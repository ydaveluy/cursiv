// The subset construction over every mode of a lexer, into one numbering: a
// subset reached from two modes is one state, and so are subsets that read
// the same under every byte class. The engine writes what a state accepts.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cursiv/shared/gen/PartitionRefine.hpp"
#include "cursiv/utils/StrongComponents.hpp"
#include "cursiv/shared/gen/ChainIndex.hpp"
#include "cursiv/shared/gen/LexAlphabet.hpp"
#include "cursiv/shared/gen/LexKeywordCut.hpp"
#include "cursiv/shared/gen/LexNfaPool.hpp"
#include "cursiv/shared/gen/LexRelayGen.hpp"
#include "cursiv/shared/gen/LowerVerdicts.hpp"
#include "cursiv/shared/gen/bit_words.hpp"
#include "cursiv/shared/gen/lex_intern_set.hpp"
#include "cursiv/shared/table/ByteSet.hpp"
#include "cursiv/shared/table/DynSet.hpp"
#include "cursiv/shared/table/TermInfo.hpp"
#include "cursiv/shared/table/Token.hpp"

namespace cursiv::engine {

// The subsets of a lexer's DFA, as bit rows in one arena: a subset costs the
// words it spans, and its bit order is its canonical order.
struct LexNfaSubsets {
  std::size_t words = 0;
  // The rows, packed to the span each one occupies: a subset of an NFA of
  // thousands of states touches a handful of words, and hashing or comparing
  // the whole width would put the cost of the widest set on every transition.
  std::vector<std::uint64_t> pool;
  std::vector<std::uint32_t> off, lo, hi;
  std::vector<std::uint64_t> cur;  // the one being built
  std::vector<std::size_t> touched;  // the words of `cur` that became non-zero
  std::size_t tn = 0;      // ...and how many of them, the room being fixed
  std::vector<int> stack;  // the epsilon walk, explicit
  std::size_t curLo = 0, curHi = 0;
  // Sized for the subsets, which number with the NFA's states.
  ChainIndex ix;

  void init(std::size_t nstates) {
    // An empty subset keeps its span empty (lo > hi) even where the NFA has
    // no state at all.
    words = std::max<std::size_t>(1, (nstates + 63) / 64);
    pool.clear();
    off.clear();
    lo.clear();
    hi.clear();
    cur.assign(words, 0);
    // Room for the deepest walk: the mark is tested at the pop, and a state
    // expands at most once, pushing at most two.
    stack.assign(nstates * 2 + 4, 0);
    touched.assign(words, 0);
    tn = 0;
    curLo = words;
    curHi = 0;
    ix.init(nstates);
  }
  std::size_t count() const { return off.size(); }

  void close(const LexNfaPool& n, int s) {
    if (s < 0) return;
    int* const st = stack.data();
    const LexNfaState* const ns = n.s.data();
    std::uint64_t* const cw = cur.data();
    std::size_t* const tw = touched.data();
    std::size_t sp = 0;
    st[sp++] = s;
    while (sp != 0) {
      const int x = st[--sp];
      const std::size_t k = static_cast<std::size_t>(x) >> 6;
      const std::uint64_t b = std::uint64_t{1} << (x & 63);
      if (cw[k] & b) continue;
      if (cw[k] == 0) tw[tn++] = k;
      cw[k] |= b;
      curLo = std::min(k, curLo);
      curHi = std::max(k, curHi);
      const LexNfaState& e = ns[static_cast<std::size_t>(x)];
      if (e.set >= 0) continue;
      if (e.t1 >= 0) st[sp++] = e.t1;
      if (e.t2 >= 0) st[sp++] = e.t2;
    }
  }

  // Interns the row being built and clears it: every word it holds is in
  // `touched`, so zeroing those words empties it whole. Equal content gives
  // the same span, so the span is part of the identity and not a hint.
  unsigned intern() {
    std::uint64_t* const cw = cur.data();
    const std::uint32_t* const lop = lo.data();
    const std::uint32_t* const hip = hi.data();
    const std::uint32_t* const offp = off.data();
    const std::uint64_t* const pp = pool.data();
    std::uint64_t h = hash_mix(0x9e3779b97f4a7c15ull, curLo);
    h = hash_mix(h, curHi);
    for (std::size_t k = curLo; k <= curHi; ++k) h = hash_mix(h, cw[k]);
    unsigned found = ChainIndex::kNone;
    for (unsigned i = ix.first(h); i != ChainIndex::kNone; i = ix.after(i)) {
      if (lop[i] != curLo || hip[i] != curHi) continue;
      bool same = true;
      const std::size_t b = offp[i];
      for (std::size_t k = curLo; k <= curHi && same; ++k)
        same = pp[b + (k - curLo)] == cw[k];
      if (same) {
        found = i;
        break;
      }
    }
    if (found == ChainIndex::kNone) {
      found = static_cast<unsigned>(off.size());
      off.push_back(static_cast<std::uint32_t>(pool.size()));
      lo.push_back(static_cast<std::uint32_t>(curLo));
      hi.push_back(static_cast<std::uint32_t>(curHi));
      for (std::size_t k = curLo; k <= curHi; ++k) pool.push_back(cw[k]);
      ix.add(h, found);
    }
    const std::size_t* const tw = touched.data();
    for (std::size_t q = 0; q < tn; ++q) cw[tw[q]] = 0;
    tn = 0;
    curLo = words;
    curHi = 0;
    return found;
  }
};

// What one subset accepts: its terminals, each behind the boundaries its
// readings stand behind, and the relays it launches.
struct LexDfaReading {
  struct Accept {
    std::uint16_t term;
    std::uint16_t bound;  // `kNoSet` for a reading with none
    // A keyword where the bound is a word class, else a literal where a
    // literal's branch reads it.
    std::uint8_t priority = 0;
    auto operator<=>(const Accept&) const = default;
  };
  // A trivia the subset gave up because a keyword's spelling stands beside it:
  // launched into the subset `resume` where none of the keywords of `gated`
  // stands.
  struct Gate {
    std::uint16_t term;
    std::uint16_t resume;
  };
  // By terminal, then boundary, each once. A terminal read with no boundary
  // keeps that reading alone.
  std::vector<Accept> accepts;
  // ...and the word-bounded readings that one hides: a keyword among the
  // branches still reserves where it stands.
  std::vector<Accept> stands;
  std::vector<std::uint16_t> relays;  // ascending, each once
  std::vector<Gate> gates;
  // The keywords every gate waits on, as `LexKeywordCut::reads` tags them;
  // empty where there is no gate.
  std::vector<std::uint32_t> gated;
  DynSet acc;                         // the terminals of `accepts`
  int best = -1;                      // the terminal the lexer elects
  // An acceptance stands behind the refusal of a scalar's rest.
  bool behindLoneLead = false;

  // The boundary of the elected terminal's first reading.
  std::uint16_t best_bound() const {
    for (const Accept& a : accepts)
      if (a.term == best) return a.bound;
    return kNoSet;
  }

  void key(std::vector<std::uint64_t>& out) const {
    for (const Accept& a : accepts)
      out.push_back((std::uint64_t{a.priority} << 32) |
                    (std::uint64_t{a.term} << 16) | a.bound);
    out.push_back(~std::uint64_t{0});
    for (const Accept& a : stands)
      out.push_back((std::uint64_t{a.term} << 16) | a.bound);
    out.push_back(~std::uint64_t{0} - 1u);
    for (const std::uint16_t r : relays) out.push_back(r);
    for (const Gate& gate : gates)
      out.push_back((std::uint64_t{gate.term} << 16) | gate.resume);
    out.push_back(gated.size());
    for (const std::uint32_t tag : gated) out.push_back(tag);
  }
};

struct LexDfaBuild {
  LexDfaBuild(const LexNfaPool& n, const ByteSet* sets, std::size_t nsets,
              const std::vector<unsigned>& repByte, unsigned classes,
              const LexKeywordCut& cut, const TermInfo* terms,
              std::span<const std::uint8_t> words = {})
      : _nfa(n),
        _cut(cut),
        _terms(terms),
        _words(words),
        _ncls(classes),
        _clsWords((classes + 63u) / 64u),
        _clsOf(n.s.size() * _clsWords, 0),
        _mem(n.s.size() + 1, 0),
        _memMask(std::size_t{classes} * ((n.s.size() + 63u) / 64u) + 1u, 0),
        _zeroRow((n.s.size() + 63u) / 64u, 0) {
    // The question is the charset's and not the state's: far fewer sets
    // answer for far more states, so a state's mask is two words of
    // arithmetic instead of a walk of the sets.
    std::vector<std::uint64_t> setCls(nsets * _clsWords, 0);
    for (std::size_t b = 0; b < nsets; ++b)
      for (unsigned c = 0; c < _ncls; ++c)
        if (sets[b].has(static_cast<unsigned char>(repByte[c])))
          setCls[b * _clsWords + (c >> 6)] |= std::uint64_t{1} << (c & 63);
    for (std::size_t x = 0; x < n.s.size(); ++x) {
      const LexNfaState& st = n.s[x];
      if (st.set < 0) continue;
      const std::uint64_t* const in =
          setCls.data() + static_cast<std::size_t>(st.set) * _clsWords;
      std::uint64_t* const row = _clsOf.data() + x * _clsWords;
      if (st.minus < 0) {
        for (std::size_t k = 0; k < _clsWords; ++k) row[k] = in[k];
      } else {
        const std::uint64_t* const out =
            setCls.data() + static_cast<std::size_t>(st.minus) * _clsWords;
        for (std::size_t k = 0; k < _clsWords; ++k) row[k] = in[k] & ~out[k];
      }
    }
    _sub.init(n.s.size());
    _kernelIx.init(n.s.size());
    _wide.assign(classes, 0);
    for (unsigned c = 0; c < classes; ++c) _wide[c] = repByte[c] >= 0x80;
  }

  void close(int s) { _sub.close(_nfa, s); }

  // The subset built so far, interned and numbered at its first reach.
  std::uint16_t intern() {
    const std::uint32_t id = number(_sub.intern());
    if (id >= kDeadState) throw LowerRefused(LowerFail::tokenGraphTooLarge);
    _roots.push_back(id);
    return static_cast<std::uint16_t>(id);
  }

  // A mode's start: the subset of every terminal it lexes.
  std::uint16_t start(const DynSet& terms, const std::vector<int>& startOf) {
    for (unsigned t = 1; t < startOf.size(); ++t)
      if (terms.test(t)) close(startOf[t]);
    return intern();
  }

  // The state a subset numbered by `start` or `intern` became, once `fill` is
  // done.
  std::uint16_t final_of(std::uint16_t subset) const { return _final[subset]; }

  // Every state, those its rows reach included, the subsets that read the same
  // under every byte class merged into the first of them.
  // `write(state, reading)` writes what the state accepts.
  template <class Built, class Write>
  void fill(Built& A, Write&& write) {
    std::vector<LexDfaReading> readings;
    std::vector<std::uint32_t> rows;
    std::vector<std::uint32_t> cls;
    std::vector<std::uint32_t> plain;
    std::unordered_map<std::string, std::uint32_t> ids;
    std::vector<std::uint64_t> key;
    const auto intern_key = [&](const LexDfaReading& r) {
      key.clear();
      r.key(key);
      std::string v(reinterpret_cast<const char*>(key.data()),
                    key.size() * sizeof(std::uint64_t));
      const auto id = static_cast<std::uint32_t>(ids.size());
      return ids.try_emplace(std::move(v), id).first->second;
    };
    bool behindLoneLead = false;
    for (std::size_t i = 0; i < _sub.count(); ++i) {
      std::vector<LexDfaReading::Gate> gates;
      std::vector<std::uint32_t> gated;
      const std::size_t mn =
          detach(members(static_cast<std::uint32_t>(i)), gates, gated);
      readings.push_back(read(mn));
      readings.back().gates = std::move(gates);
      readings.back().gated = std::move(gated);
      cls.push_back(intern_key(readings.back()));
      plain.push_back(cls.back());
      if (readings.back().behindLoneLead) {
        behindLoneLead = true;
        auto bare = read(mn, true);
        bare.gates = readings.back().gates;
        bare.gated = readings.back().gated;
        plain.back() = intern_key(bare);
      }
      rows.resize((i + 1) * _ncls);
      row(mn, rows.data() + i * _ncls);
    }
    const std::size_t n = _sub.count();
    std::uint32_t nblocks = 0;
    if (behindLoneLead) {
      std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> pairs;
      std::vector<std::uint32_t> halves(n);
      for (std::size_t s = 0; s < n; ++s)
        halves[s] = pairs
                        .try_emplace({cls[s], plain[s]},
                                     static_cast<std::uint32_t>(pairs.size()))
                        .first->second;
      const auto exact = blocks(halves, rows, nblocks);
      collapse(exact, nblocks, rows, cls, plain, readings);
    }
    const auto blk = blocks(cls, rows, nblocks);
    std::vector<char> live(n, 0);
    {
      auto work = _roots;
      for (const std::uint32_t r : _roots) live[r] = 1;
      const auto reach = [&](std::uint32_t t) {
        if (t != kNoLocal && !live[t]) {
          live[t] = 1;
          work.push_back(t);
        }
      };
      while (!work.empty()) {
        const auto s = work.back();
        work.pop_back();
        for (unsigned c = 0; c < _ncls; ++c) reach(rows[s * _ncls + c]);
        for (const LexDfaReading::Gate& gate : readings[s].gates)
          reach(gate.resume);
      }
    }
    std::vector idOf(nblocks, kDeadState);
    std::vector<std::uint32_t> rep;
    _final.assign(n, kDeadState);
    for (std::size_t s = 0; s < n; ++s) {
      if (!live[s]) continue;
      std::uint16_t& id = idOf[blk[s]];
      if (id == kDeadState) {
        if (rep.size() >= kDeadState)
          throw LowerRefused(LowerFail::tokenGraphTooLarge);
        id = static_cast<std::uint16_t>(rep.size());
        rep.push_back(static_cast<std::uint32_t>(s));
      }
      _final[s] = id;
    }
    A.ndfa = static_cast<std::uint16_t>(rep.size());
    for (std::size_t i = 0; i < rep.size(); ++i) {
      write(static_cast<std::uint16_t>(i), readings[rep[i]]);
      std::uint16_t* const dst = A.dfaNext.row(i * _ncls, _ncls);
      const std::uint32_t* const src =
          rows.data() + std::size_t{rep[i]} * _ncls;
      for (unsigned c = 0; c < _ncls; ++c)
        dst[c] = src[c] == kNoLocal ? kDeadState : _final[src[c]];
    }
  }

 private:
  static constexpr std::uint32_t kNoLocal = 0xffffffffu;
  // The subsets one build may number before it refuses: eight times the states
  // the reduced DFA may have, so a terminal such as `(a|b)* a (a|b){24}` is
  // refused before it holds millions of subsets.
  static constexpr std::size_t kMaxSubsets = std::size_t{8} * kDeadState;

  // The coarsest refinement of `cls` every byte class respects. Blocks are
  // numbered by their first state, the dead row apart.
  std::vector<std::uint32_t> blocks(const std::vector<std::uint32_t>& cls,
                                    const std::vector<std::uint32_t>& rows,
                                    std::uint32_t& count) const {
    const std::size_t n = cls.size(), k = _ncls;
    const auto dead = static_cast<std::uint32_t>(n);
    std::uint32_t initial = 0;
    for (const std::uint32_t c : cls) initial = std::max(initial, c + 1);
    std::vector<std::uint32_t> opening(cls);
    opening.push_back(initial);
    const std::vector<std::uint32_t> blk = partition_refine(
        opening, static_cast<std::uint32_t>(k), [&](auto&& edge) {
          for (std::size_t s = 0; s <= n; ++s)
            for (std::size_t c = 0; c < k; ++c) {
              const std::uint32_t r = s == n ? kNoLocal : rows[s * k + c];
              edge(static_cast<std::uint32_t>(s), static_cast<std::uint32_t>(c),
                   r == kNoLocal ? dead : r);
            }
        });
    std::vector<std::uint32_t> id(*std::ranges::max_element(blk) + 1u, kNoLocal),
        out(n);
    count = 0;
    for (std::size_t s = 0; s < n; ++s) {
      std::uint32_t& b = id[blk[s]];
      if (b == kNoLocal) b = count++;
      out[s] = b;
    }
    return out;
  }

  // A state whose wide bytes all lead into a region of states that read the
  // same on every other byte reads them in one stride, into the least state of
  // the region that does not refuse a scalar's rest. Decided per strong
  // component.
  void collapse(const std::vector<std::uint32_t>& blk, std::uint32_t nblocks,
                std::vector<std::uint32_t>& rows,
                const std::vector<std::uint32_t>& strict,
                const std::vector<std::uint32_t>& plain,
                const std::vector<LexDfaReading>& readings) const {
    const std::size_t k = _ncls, n = blk.size();
    std::vector<unsigned> wide, narrow;
    for (unsigned c = 0; c < _ncls; ++c)
      (_wide[c] ? wide : narrow).push_back(c);
    if (wide.empty()) return;
    std::vector head(nblocks, kNoLocal);
    for (std::size_t s = n; s-- > 0;)
      head[blk[s]] = static_cast<std::uint32_t>(s);
    const auto to = [&](std::uint32_t b, unsigned c) {
      const auto r = rows[std::size_t{head[b]} * k + c];
      return r == kNoLocal ? kNoLocal : blk[r];
    };
    const auto clean = [&](std::uint32_t b) {
      const auto& r = readings[head[b]];
      return r.relays.empty() && r.gates.empty();
    };
    std::vector<std::uint32_t> starts{0}, edges;
    for (std::uint32_t b = 0; b < nblocks; ++b) {
      for (const unsigned c : wide) edges.push_back(to(b, c));
      starts.push_back(static_cast<std::uint32_t>(edges.size()));
    }
    const auto scc = StrongComponents<>::build(starts, edges);
    // What a component and all it reaches say: whether each state is clean
    // and lives on every wide byte, a state they all read the same with
    // (`kNoLocal` where they do not), and their least state with no refusal.
    struct Region {
      bool total = true;
      std::uint32_t like = kNoLocal, least = kNoLocal;
    };
    const auto equivalent = [&](std::uint32_t y, std::uint32_t b) {
      return y == b ||
             (plain[head[y]] == plain[head[b]] &&
              std::ranges::all_of(
                  narrow, [&](unsigned c) { return to(y, c) == to(b, c); }));
    };
    const auto join = [&](Region& into, const Region& r) {
      into.total = into.total && r.total;
      into.least = std::min(into.least, r.least);
      if (into.like != kNoLocal &&
          (r.like == kNoLocal || !equivalent(into.like, r.like)))
        into.like = kNoLocal;
    };
    // A component closes after all it reaches: in that order, each region is
    // joined from those already built.
    std::vector<std::uint32_t> order(nblocks);
    for (std::uint32_t b = 0; b < nblocks; ++b) order[b] = b;
    std::ranges::sort(order, {},
                      [&](std::uint32_t b) { return scc.component[b]; });
    std::vector<Region> region(nblocks);
    const Region dead{false};
    const auto reach = [&](std::uint32_t t) -> const Region& {
      return t == kNoLocal ? dead : region[scc.component[t]];
    };
    for (std::size_t i = 0; i < order.size(); ++i) {
      const std::uint32_t y = order[i], own = scc.component[y];
      const Region self{clean(y), y,
                        strict[head[y]] == plain[head[y]] ? y : kNoLocal};
      if (i == 0 || scc.component[order[i - 1]] != own)
        region[own] = self;
      else
        join(region[own], self);
      for (auto e = starts[y]; e < starts[y + 1]; ++e)
        if (edges[e] == kNoLocal || scc.component[edges[e]] != own)
          join(region[own], reach(edges[e]));
    }
    std::vector target(nblocks, kNoLocal);
    for (std::uint32_t x = 0; x < nblocks; ++x) {
      if (!clean(x)) continue;
      Region r = reach(edges[starts[x]]);
      for (auto e = starts[x] + 1; e < starts[x + 1]; ++e)
        join(r, reach(edges[e]));
      if (r.total && r.like != kNoLocal) target[x] = r.least;
    }
    for (std::size_t s = 0; s < n; ++s)
      if (const auto b = target[blk[s]]; b != kNoLocal)
        for (const unsigned c : wide) rows[s * k + c] = head[b];
  }

  const LexNfaPool& _nfa;
  const LexKeywordCut& _cut;
  const TermInfo* const _terms;
  const std::span<const std::uint8_t> _words;
  const unsigned _ncls;
  const std::size_t _clsWords;
  // What an NFA state consumes, as a mask of byte classes.
  std::vector<std::uint64_t> _clsOf;
  // One pool for every mode: the transition of a subset is a function of the
  // subset, so a closure is computed once whatever number of modes reach it.
  LexNfaSubsets _sub;
  std::vector<int> _mem;  // the members of the state being filled
  // One word per class, one bit per member: what a class consumes is a subset
  // of the members, and a subset holds a handful of them.
  std::vector<std::uint64_t> _memMask;
  const std::vector<std::uint64_t> _zeroRow;
  std::vector<std::uint8_t> _wide;    // the byte classes wholly past ASCII
  std::vector<std::uint32_t> _roots;  // the subsets the caller holds a state of
  std::vector<std::uint16_t> _final;    // subset -> the state it became
  // A transition's subset is the closure of the states its members step to:
  // one kernel is one subset, closed once however many transitions share it.
  ChainIndex _kernelIx;
  std::vector<int> _kernelPool;
  std::vector<std::uint32_t> _kernelAt{0};
  std::vector<std::uint32_t> _kernelSub;
  std::vector<int> _kernel;

  // A subset is numbered by the pool as it is first reached: past
  // `kMaxSubsets` the terminal is refused.
  static std::uint32_t number(std::uint32_t gid) {
    if (gid >= kMaxSubsets) throw LowerRefused(LowerFail::tokenGraphTooLarge);
    return gid;
  }

  std::uint32_t close_kernel() {
    std::ranges::sort(_kernel);
    _kernel.erase(std::ranges::unique(_kernel).begin(), _kernel.end());
    std::uint64_t h = hash_mix(0x9e3779b97f4a7c15ull, _kernel.size());
    for (const int t : _kernel) h = hash_mix(h, static_cast<std::uint64_t>(t));
    for (unsigned i = _kernelIx.first(h); i != ChainIndex::kNone;
         i = _kernelIx.after(i))
      if (std::equal(_kernel.begin(), _kernel.end(),
                     _kernelPool.begin() + _kernelAt[i],
                     _kernelPool.begin() + _kernelAt[i + 1]))
        return _kernelSub[i];
    for (const int t : _kernel) _sub.close(_nfa, t);
    const std::uint32_t gid = _sub.intern();
    const auto k = static_cast<unsigned>(_kernelSub.size());
    _kernelPool.insert(_kernelPool.end(), _kernel.begin(), _kernel.end());
    _kernelAt.push_back(static_cast<std::uint32_t>(_kernelPool.size()));
    _kernelSub.push_back(gid);
    _kernelIx.add(h, k);
    return gid;
  }

  bool word_bound(std::uint16_t bound) const {
    return bound != kNoSet && bound < _words.size() && _words[bound] != 0;
  }

  // What the first `mn` members accept and launch. A `plain` read only decides
  // the acceptance, for the subset's key: `acc`, `best` and `behindLoneLead`
  // stay unset.
  LexDfaReading read(std::size_t mn, bool plain = false) const {
    LexDfaReading r;
    const LexNfaState* const nst = _nfa.s.data();
    for (std::size_t q = 0; q < mn; ++q) {
      const LexNfaState& st = nst[static_cast<std::size_t>(_mem[q])];
      if (st.relay >= 0) r.relays.push_back(static_cast<std::uint16_t>(st.relay));
      if (st.accept >= 0) {
        if (!plain) r.behindLoneLead = r.behindLoneLead || st.loneLeadBound;
        const auto own = plain ? st.plain() : st.bound;
        const auto bound = own < 0 ? kNoSet : static_cast<std::uint16_t>(own);
        r.accepts.push_back(
            {static_cast<std::uint16_t>(st.accept), bound,
             static_cast<std::uint8_t>(word_bound(bound) ? kKeywordPriority
                                       : st.lit          ? kLiteralPriority
                                                         : 0)});
      }
    }
    std::ranges::sort(r.relays);
    r.relays.erase(std::ranges::unique(r.relays).begin(), r.relays.end());
    auto& rows = r.accepts;
    std::ranges::sort(rows);
    auto bestRank = 0u;
    std::size_t kept = 0;
    for (std::size_t i = 0, j; i < rows.size(); i = j) {
      for (j = i; j < rows.size() && rows[j].term == rows[i].term;) ++j;
      if (!plain) {
        const auto term = rows[i].term;
        r.acc.set(term);
        auto priority = 0u;
        for (std::size_t k = i; k < j; ++k)
          priority = std::max<unsigned>(priority, rows[k].priority);
        const auto rank = lex_rank(priority, _terms[term].trivia);
        if (r.best < 0 || lex_term_before(rank, term, bestRank,
                                          static_cast<unsigned>(r.best))) {
          r.best = term;
          bestRank = rank;
        }
      }
      if (rows[j - 1].bound == kNoSet) {
        for (std::size_t k = i; k + 1 < j; ++k)
          if (word_bound(rows[k].bound) &&
              (k == i || rows[k].bound != rows[k - 1].bound))
            r.stands.push_back(rows[k]);
        rows[kept++] = rows[j - 1];
        continue;
      }
      for (std::size_t k = i; k < j; ++k)
        if (k + 1 == j || rows[k].bound != rows[k + 1].bound)
          rows[kept++] = rows[k];
    }
    rows.resize(kept);
    return r;
  }

  std::size_t members(std::uint32_t gid) {
    const std::size_t glo = _sub.lo[gid], ghi = _sub.hi[gid];
    int* const mw = _mem.data();
    std::size_t mn = 0;
    // An empty subset is interned with lo > hi.
    each_bit(
        _sub.pool.data() + _sub.off[gid], ghi >= glo ? ghi - glo + 1 : 0,
        [&](std::size_t b) { mw[mn++] = static_cast<int>((glo << 6) + b); });
    return mn;
  }

  // Trivia beside a keyword goes on only behind `gates` if it holds a relay,
  // and stops once the keyword is spelled whole otherwise. Returns the number
  // of members left.
  std::size_t detach(std::size_t mn, std::vector<LexDfaReading::Gate>& gates,
                     std::vector<std::uint32_t>& gated) {
    if (!_cut.active) return mn;
    int* const mw = _mem.data();
    std::vector<std::uint32_t> stands;
    std::vector<std::uint16_t> relayed;
    bool whole = false;
    for (std::size_t q = 0; q < mn; ++q) {
      const auto x = static_cast<std::size_t>(mw[q]);
      for (const std::uint32_t tag : _cut.reads[x]) {
        stands.push_back(tag);
        whole = whole || _cut.whole(tag);
      }
      if (_cut.trivia[x] != 0 && _nfa.s[x].relay >= 0)
        relayed.push_back(_cut.trivia[x]);
    }
    if (stands.empty()) return mn;
    std::ranges::sort(stands);
    stands.erase(std::ranges::unique(stands).begin(), stands.end());
    const auto leaves = [&](int x) {
      const LexNfaState& st = _nfa.s[static_cast<std::size_t>(x)];
      const std::uint16_t term = _cut.trivia[static_cast<std::size_t>(x)];
      return term != 0 && (st.set >= 0 || st.relay >= 0) &&
             (whole || std::ranges::find(relayed, term) != relayed.end());
    };
    std::map<std::uint16_t, std::vector<int>> apart;
    std::size_t kept = 0;
    for (std::size_t q = 0; q < mn; ++q) {
      const int x = mw[q];
      if (leaves(x))
        apart[_cut.trivia[static_cast<std::size_t>(x)]].push_back(x);
      else
        mw[kept++] = x;
    }
    for (const auto& [term, from] : apart) {
      for (const int x : from) _sub.close(_nfa, x);
      const std::uint32_t resume = number(_sub.intern());
      if (resume >= kDeadState)
        throw LowerRefused(LowerFail::tokenGraphTooLarge);
      gates.push_back({term, static_cast<std::uint16_t>(resume)});
    }
    if (!gates.empty()) gated = std::move(stands);
    return kept;
  }

  void row(std::size_t mn, std::uint32_t* const nrow) {
    const int* const mw = _mem.data();
    const LexNfaState* const nst = _nfa.s.data();
    std::uint64_t* const mm = _memMask.data();
    // Which members a class consumes, as a mask. One pass over the members'
    // own bits marks them, and a class then walks only its own, in member
    // order: the closures, and the numbering they drive, follow that order.
    const std::size_t mWordsM = (mn + 63u) / 64u;
    std::fill_n(mm, static_cast<std::size_t>(_ncls) * mWordsM, 0);
    for (std::size_t q = 0; q < mn; ++q) {
      const std::uint64_t* const cr =
          _clsOf.data() + static_cast<std::size_t>(mw[q]) * _clsWords;
      const std::size_t qw = q >> 6;
      const std::uint64_t qb = std::uint64_t{1} << (q & 63);
      each_bit(cr, _clsWords,
               [&](std::size_t c) { mm[c * mWordsM + qw] |= qb; });
    }
    // A class whose mask repeats the previous class's goes to the same target.
    // A zero row stands for no class answered yet.
    const std::uint64_t* prevRow = _zeroRow.data();
    std::uint32_t prevAt = kNoLocal;
    for (unsigned c = 0; c < _ncls; ++c) {
      const std::uint64_t* const row =
          mm + static_cast<std::size_t>(c) * mWordsM;
      bool none = true, same = true;
      if (mWordsM == 1) {
        none = row[0] == 0;
        same = row[0] == prevRow[0];
      } else {
        for (std::size_t k = 0; k < mWordsM; ++k) {
          none &= row[k] == 0;
          same &= row[k] == prevRow[k];
        }
      }
      if (none) {
        nrow[c] = kNoLocal;
        continue;
      }
      if (same) {
        nrow[c] = prevAt;
        continue;
      }
      _kernel.clear();
      each_bit(row, mWordsM, [&](std::size_t q) {
        _kernel.push_back(nst[static_cast<std::size_t>(mw[q])].t1);
      });
      prevAt = number(close_kernel());
      nrow[c] = prevAt;
      prevRow = row;
    }
  }
};

// The DFA of every mode of `modeSet` into `b`. `write(state, reading)` writes
// what a state accepts, its relay chain already in `b.dfaRelay`; `push`
// appends a row to `b.relays` and returns its index.
template <class Built, class Push, class Write>
void lex_build_dfa(Built& b, const LexAlphabet& alpha, LexKeywordCut& cut,
                   const std::vector<DynSet>& modeSet, const Push& push,
                   const Write& write) {
  auto& g = b.cfg;
  const std::vector<std::uint8_t> words = lex_word_flags(g);
  LexDfaBuild dfa(alpha.nfa, g.sets.data(), g.sets.size(), alpha.repByte,
                  alpha.ncls, cut, g.terms.data(), words);
  LexRelayChains chains;
  for (unsigned m = 0; m < b.nmodes; ++m)
    b.modeStart[m] = dfa.start(modeSet[m], alpha.startOf);
  lex_relay_seed(dfa, alpha.nfa.relays, push);
  dfa.fill(b, [&](std::uint16_t self, const LexDfaReading& r) {
    b.dfaRelay[self] = chains.launched(g, cut, r, b.relays, push);
    write(self, r);
  });
  for (unsigned m = 0; m < b.nmodes; ++m)
    b.modeStart[m] = dfa.final_of(b.modeStart[m]);
  for (std::size_t k = 0; k < b.relays.size(); ++k)
    b.relays[k].resume = dfa.final_of(b.relays[k].resume);
}

}  // namespace cursiv::engine
