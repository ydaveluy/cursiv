// The subset construction over every mode of a lexer, into one numbering: a
// subset reached from two modes is one state, so a mode is a start and not a
// range; subsets that read alike under every byte class are one state too. The
// rows are this; what a state lays of what it accepts is the engine's.
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

#include "cursiv/engines/shared/gen/ChainIndex.hpp"
#include "cursiv/engines/shared/gen/LexKeywordCut.hpp"
#include "cursiv/engines/shared/gen/LexNfaPool.hpp"
#include "cursiv/engines/shared/gen/LexNfaSubsets.hpp"
#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"
#include "cursiv/engines/shared/gen/bit_words.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/engines/shared/table/DynSet.hpp"
#include "cursiv/engines/shared/table/TermInfo.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

// What one subset accepts: its terminals, each behind the boundaries its
// readings stand behind, and the relays it launches.
struct LexDfaReading {
  struct Accept {
    std::uint16_t term;
    std::uint16_t bound;  // `kNoSet` for a reading with none
    // A keyword where the bound is a word class, else a literal where a
    // literal's branch reads it.
    std::uint8_t tier = 0;
    auto operator<=>(const Accept&) const = default;
  };
  // A trivia the subset gave up because a keyword's spelling stands beside it:
  // launched into the subset `resume` where none of the keywords `stands`
  // names (as `LexKeywordCut::reads` tags them) stands.
  struct Gate {
    std::uint16_t term;
    std::uint16_t resume;
    std::vector<std::uint32_t> stands;
  };
  // By terminal, then boundary, each once. A terminal read with no boundary
  // keeps that reading alone.
  std::vector<Accept> accepts;
  // ...and the word-bounded readings that one hides: a keyword among the
  // branches still reserves where it stands.
  std::vector<Accept> stands;
  std::vector<std::uint16_t> relays;  // ascending, each once
  std::vector<Gate> gates;
  DynSet acc;                         // the terminals of `accepts`
  int best = -1;                      // the terminal the lexer elects
  // An acceptance stands behind the refusal of a scalar's rest.
  bool strayed = false;

  // The boundary of the elected terminal's first reading.
  std::uint16_t best_bound() const {
    for (const Accept& a : accepts)
      if (a.term == best) return a.bound;
    return kNoSet;
  }

  void key(std::vector<std::uint64_t>& out) const {
    for (const Accept& a : accepts)
      out.push_back((std::uint64_t{a.tier} << 32) |
                    (std::uint64_t{a.term} << 16) | a.bound);
    out.push_back(~std::uint64_t{0});
    for (const Accept& a : stands)
      out.push_back((std::uint64_t{a.term} << 16) | a.bound);
    out.push_back(~std::uint64_t{0} - 1u);
    for (const std::uint16_t r : relays) out.push_back(r);
    for (const Gate& gate : gates) {
      out.push_back((std::uint64_t{gate.term} << 16) | gate.resume);
      out.push_back(gate.stands.size());
      for (const std::uint32_t tag : gate.stands) out.push_back(tag);
    }
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

  // Every state, those its rows reach included, the subsets that read alike
  // under every byte class merged into the first of them: `write(state,
  // reading)` lays what the state accepts.
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
    bool strayed = false;
    for (std::size_t i = 0; i < _order.size(); ++i) {
      std::vector<LexDfaReading::Gate> gates;
      const std::size_t mn = detach(members(_order[i]), gates);
      readings.push_back(read(mn));
      readings.back().gates = std::move(gates);
      cls.push_back(intern_key(readings.back()));
      plain.push_back(cls.back());
      if (readings.back().strayed) {
        strayed = true;
        auto bare = read(mn, true);
        bare.gates = readings.back().gates;
        plain.back() = intern_key(bare);
      }
      rows.resize((i + 1) * _ncls);
      row(mn, rows.data() + i * _ncls);
    }
    const std::size_t n = _order.size();
    std::uint32_t nblocks = 0;
    if (strayed) {
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
    std::vector<std::uint16_t> idOf(nblocks, kDeadState);
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

  // The coarsest refinement of `cls` every byte class respects, by Hopcroft's
  // splitting: a split queues its smaller half, so no state moves more than
  // log n times. Blocks are numbered by their first state, the dead row apart.
  std::vector<std::uint32_t> blocks(const std::vector<std::uint32_t>& cls,
                                    const std::vector<std::uint32_t>& rows,
                                    std::uint32_t& count) const {
    const std::size_t n = cls.size(), k = _ncls;
    const auto dead = static_cast<std::uint32_t>(n);
    const auto to = [&](std::size_t s, std::size_t c) {
      const std::uint32_t r = s == n ? kNoLocal : rows[s * k + c];
      return r == kNoLocal ? dead : r;
    };
    // `from[at[t * k + c]]` up to `from[at[t * k + c + 1]]` reach `t` on `c`.
    std::vector<std::uint32_t> at((n + 1) * k + 1, 0), from((n + 1) * k);
    for (std::size_t s = 0; s <= n; ++s)
      for (std::size_t c = 0; c < k; ++c) ++at[to(s, c) * k + c + 1];
    for (std::size_t i = 1; i < at.size(); ++i) at[i] += at[i - 1];
    {
      std::vector<std::uint32_t> put(at.begin(), at.end() - 1);
      for (std::size_t s = 0; s <= n; ++s)
        for (std::size_t c = 0; c < k; ++c)
          from[put[to(s, c) * k + c]++] = static_cast<std::uint32_t>(s);
    }
    // Block `b` is `elem[first[b]]` up to `elem[last[b]]`; a pass moves the
    // states it marks to the front, up to `mid[b]`.
    std::uint32_t initial = 0;
    for (const std::uint32_t c : cls) initial = std::max(initial, c + 1);
    std::vector<std::uint32_t> blk(n + 1), elem(n + 1), pos(n + 1);
    std::vector<std::uint32_t> first(initial + 2, 0), last, mid;
    for (std::size_t s = 0; s <= n; ++s) {
      blk[s] = s == n ? initial : cls[s];
      ++first[blk[s] + 1];
    }
    for (std::size_t b = 1; b < first.size(); ++b) first[b] += first[b - 1];
    last.assign(first.begin() + 1, first.end());
    first.pop_back();
    mid = first;
    for (std::size_t s = 0; s <= n; ++s) {
      pos[s] = mid[blk[s]]++;
      elem[pos[s]] = static_cast<std::uint32_t>(s);
    }
    mid = first;
    std::vector<std::uint32_t> work(first.size()), splitter, touched;
    for (std::uint32_t b = 0; b < work.size(); ++b) work[b] = b;
    std::vector<char> queued(first.size(), 1);
    while (!work.empty()) {
      const std::uint32_t a = work.back();
      work.pop_back();
      queued[a] = 0;
      splitter.assign(elem.begin() + first[a], elem.begin() + last[a]);
      for (std::size_t c = 0; c < k; ++c) {
        touched.clear();
        for (const std::uint32_t t : splitter)
          for (auto i = at[t * k + c]; i < at[t * k + c + 1]; ++i) {
            const std::uint32_t p = from[i], b = blk[p];
            if (mid[b] == first[b]) touched.push_back(b);
            const std::uint32_t q = elem[mid[b]];
            elem[pos[p]] = q;
            pos[q] = pos[p];
            elem[mid[b]] = p;
            pos[p] = mid[b]++;
          }
        for (const std::uint32_t b : touched) {
          if (mid[b] == last[b]) {
            mid[b] = first[b];
            continue;
          }
          const auto part = static_cast<std::uint32_t>(first.size());
          first.push_back(first[b]);
          last.push_back(mid[b]);
          mid.push_back(first[b]);
          queued.push_back(0);
          first[b] = mid[b];
          for (auto i = first[part]; i < last[part]; ++i) blk[elem[i]] = part;
          const bool smaller = last[part] - first[part] <= last[b] - first[b];
          const std::uint32_t add = queued[b] || smaller ? part : b;
          if (!queued[add]) {
            queued[add] = 1;
            work.push_back(add);
          }
        }
      }
    }
    std::vector<std::uint32_t> id(first.size(), kNoLocal), out(n);
    count = 0;
    for (std::size_t s = 0; s < n; ++s) {
      std::uint32_t& b = id[blk[s]];
      if (b == kNoLocal) b = count++;
      out[s] = b;
    }
    return out;
  }

  // A state whose every wide byte leads into a region of states that read
  // alike -- the same row on every other byte, the same acceptance bar the
  // refusal of a scalar's rest, none dead on a wide byte -- reads them in one
  // stride into the region's state that has no such refusal. No end falls
  // inside a scalar there: the walk never dies on a continuation byte, and
  // the refusal only held an end back from the byte that continues it.
  void collapse(const std::vector<std::uint32_t>& blk, std::uint32_t nblocks,
                std::vector<std::uint32_t>& rows,
                const std::vector<std::uint32_t>& strict,
                const std::vector<std::uint32_t>& plain,
                const std::vector<LexDfaReading>& readings) const {
    const std::size_t k = _ncls, n = blk.size();
    std::vector<unsigned> wide, narrow;
    for (unsigned c = 0; c < _ncls; ++c)
      (_wide[c] ? wide : narrow).push_back(c);
    std::vector<std::uint32_t> head(nblocks, kNoLocal);
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
    std::vector<std::uint32_t> target(nblocks, kNoLocal),
        seen(nblocks, kNoLocal);
    std::vector<std::uint32_t> region;
    for (std::uint32_t x = 0; x < nblocks; ++x) {
      if (!clean(x)) continue;
      region.clear();
      bool total = true;
      const auto enter = [&](std::uint32_t t) {
        if (t == kNoLocal) {
          total = false;
        } else if (seen[t] != x) {
          seen[t] = x;
          region.push_back(t);
        }
      };
      for (const unsigned c : wide) enter(to(x, c));
      for (std::size_t i = 0; total && i < region.size(); ++i) {
        const auto y = region[i];
        total = clean(y);
        for (const unsigned c : wide) enter(to(y, c));
      }
      if (!total || region.empty()) continue;
      std::sort(region.begin(), region.end());
      for (const auto b : region) {
        if (strict[head[b]] != plain[head[b]]) continue;
        const bool alike = std::ranges::all_of(region, [&](std::uint32_t y) {
          return plain[head[y]] == strict[head[b]] &&
                 std::ranges::all_of(
                     narrow, [&](unsigned c) { return to(y, c) == to(b, c); });
        });
        if (!alike) continue;
        target[x] = b;
        break;
      }
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
  std::vector<std::uint32_t> _localOf;  // gid -> index in the DFA
  std::vector<std::uint32_t> _order;    // the subsets, in discovery order
  std::vector<std::uint16_t> _final;    // subset -> the state it became
  // A transition's subset is the closure of the states its members step to:
  // one kernel is one subset, closed once however many transitions share it.
  ChainIndex _kernelIx;
  std::vector<int> _kernelPool;
  std::vector<std::uint32_t> _kernelAt{0};
  std::vector<std::uint32_t> _kernelSub;
  std::vector<int> _kernel;

  std::uint32_t number(std::uint32_t gid) {
    if (_localOf.size() < _sub.count()) {
      // Reserved at a hint: the pool grows one subset a step, and a resize
      // re-initialises what it adds.
      if (_localOf.empty()) _localOf.reserve(_nfa.s.size() * 4 + 512);
      _localOf.resize(_sub.count(), kNoLocal);
    }
    auto& id = _localOf[gid];
    if (id == kNoLocal) {
      if (_order.size() >= kNoLocal)
        throw LowerRefused(LowerFail::tokenGraphTooLarge);
      id = static_cast<std::uint32_t>(_order.size());
      _order.push_back(gid);
    }
    return id;
  }

  std::uint32_t close_kernel() {
    std::sort(_kernel.begin(), _kernel.end());
    _kernel.erase(std::unique(_kernel.begin(), _kernel.end()), _kernel.end());
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

  LexDfaReading read(std::size_t mn, bool plain = false) const {
    LexDfaReading r;
    const LexNfaState* const nst = _nfa.s.data();
    for (std::size_t q = 0; q < mn; ++q) {
      const LexNfaState& st = nst[static_cast<std::size_t>(_mem[q])];
      if (st.relay >= 0) r.relays.push_back(static_cast<std::uint16_t>(st.relay));
      if (st.accept >= 0) {
        r.strayed = r.strayed || st.strayBound;
        const auto own = plain ? st.plain() : st.bound;
        const auto bound = own < 0 ? kNoSet : static_cast<std::uint16_t>(own);
        r.accepts.push_back(
            {static_cast<std::uint16_t>(st.accept), bound,
             static_cast<std::uint8_t>(word_bound(bound) ? kTierKeyword
                                       : st.lit          ? kTierLiteral
                                                         : 0)});
      }
    }
    std::sort(r.relays.begin(), r.relays.end());
    r.relays.erase(std::unique(r.relays.begin(), r.relays.end()),
                   r.relays.end());
    auto& rows = r.accepts;
    std::ranges::sort(rows);
    auto bestRank = 0u;
    std::size_t kept = 0;
    for (std::size_t i = 0, j; i < rows.size(); i = j) {
      for (j = i; j < rows.size() && rows[j].term == rows[i].term;) ++j;
      const auto term = rows[i].term;
      r.acc.set(term);
      auto tier = 0u;
      for (std::size_t k = i; k < j; ++k)
        tier = std::max<unsigned>(tier, rows[k].tier);
      const auto rank = lex_rank(tier, _terms[term].trivia);
      if (r.best < 0 || lex_term_before(rank, term, bestRank,
                                        static_cast<unsigned>(r.best))) {
        r.best = term;
        bestRank = rank;
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

  // A trivia holding a relay goes on only behind `gates` wherever a keyword
  // stands beside it; any other trivia, once a keyword is spelled whole. One
  // subset per terminal, and what the trivia accepts stays. Answers the
  // members left.
  std::size_t detach(std::size_t mn, std::vector<LexDfaReading::Gate>& gates) {
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
    std::sort(stands.begin(), stands.end());
    stands.erase(std::unique(stands.begin(), stands.end()), stands.end());
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
      gates.push_back({term, static_cast<std::uint16_t>(resume), stands});
    }
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
    // Two classes with the same members go to the same place: a repeat of
    // the previous class's mask is answered by the previous class's target,
    // since byte classes are cut for the whole alphabet and often coincide
    // inside one subset. A subset of sixty-four members or fewer answers
    // every class with one word, so the two questions become two
    // comparisons, not two loops; a zero row stands for "no class answered
    // yet", which a non-empty row never equals.
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
        const int t = nst[static_cast<std::size_t>(mw[q])].t1;
        if (t >= 0) _kernel.push_back(t);
      });
      if (_kernel.empty()) {
        nrow[c] = kNoLocal;
        continue;
      }
      prevAt = number(close_kernel());
      nrow[c] = prevAt;
      prevRow = row;
    }
  }
};

}  // namespace cursiv
