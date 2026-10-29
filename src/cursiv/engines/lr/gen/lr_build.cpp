// LR construction: items, closure, goto, re-fusion by partition refinement,
// lex modes and their DFAs. The transition alphabet is the terminal.
#include "cursiv/engines/lr/gen/lr_build.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <numeric>
#include <optional>
#include <ranges>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cursiv/engines/lr/gen/LrBuckets.hpp"
#include "cursiv/engines/lr/gen/LrBuildCfg.hpp"
#include "cursiv/engines/lr/gen/LrFirstSets.hpp"
#include "cursiv/engines/lr/gen/LrGotoRows.hpp"
#include "cursiv/engines/lr/gen/LrGuard.hpp"
#include "cursiv/engines/lr/gen/LrItem.hpp"
#include "cursiv/engines/lr/gen/lr_build_elect.hpp"
#include "cursiv/engines/lr/gen/lr_build_fold.hpp"
#include "cursiv/engines/lr/gen/lr_build_guards.hpp"
#include "cursiv/engines/lr/gen/lr_build_lex.hpp"
#include "cursiv/engines/lr/gen/lr_build_passes.hpp"
#include "cursiv/engines/lr/gen/lr_term_run.hpp"
#include "cursiv/engines/shared/gen/ChainIndex.hpp"
#include "cursiv/engines/shared/gen/ChainPool.hpp"
#include "cursiv/engines/shared/gen/LexOvertakes.hpp"
#include "cursiv/engines/shared/gen/PredicateModes.hpp"
#include "cursiv/engines/shared/gen/bit_words.hpp"
#include "cursiv/engines/shared/table/DynSet.hpp"
#include "cursiv/engines/shared/table/FlatMap.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {
namespace {

// One transition of the canonical graph; the leaf a shift writes is the
// symbol.
struct LrEdge {
  std::uint16_t sym = 0;
  std::uint16_t to = 0;
  // Terminal edges: the caret's call site and the enclosing rule's.
  ElementId site = kNoElement;
  ElementId outer = kNoElement;
  // The guard program that picks this edge among its symbol's, plus one.
  std::uint32_t prog = 0;
};

// What an item needs of the gap to advance: it advances where one of these
// sets of conditions holds whole.
using LrReq = std::vector<DynSet>;

// `a` as one more way in; a way that asks more than another adds nothing.
bool lr_req_add(LrReq& r, const DynSet& a) {
  if (std::ranges::any_of(r, [&](const DynSet& b) { return a.covers(b); }))
    return false;
  std::erase_if(r, [&](const DynSet& b) { return b.covers(a); });
  r.push_back(a);
  return true;
}

bool lr_req_holds(const LrReq& r, const DynSet& truth) {
  return std::ranges::any_of(r,
                             [&](const DynSet& a) { return truth.covers(a); });
}

bool lr_req_implies(const LrReq& x, const LrReq& y) {
  return std::ranges::all_of(x, [&](const DynSet& a) {
    return std::ranges::any_of(y, [&](const DynSet& b) { return a.covers(b); });
  });
}

bool lr_req_asks(const LrReq& r) { return r.size() != 1 || r[0].any(); }

void lr_req_conds(const LrReq& r, DynSet& out) {
  for (const DynSet& a : r) out |= a;
}

// Two chains that lead with one write say one thing to a caret: `(name=ID)?`
// mints a chain per branch. A fused id stands above the pool, unbounded.
bool lr_same_site(const ChainPool& w, ElementId a, ElementId b) {
  if (a == b) return true;
  if (a >= w.nchains || b >= w.nchains) return false;
  const std::uint16_t lead = w.chainBuild[a].lead;
  return lead != kNoRow && lead == w.chainBuild[b].lead;
}

void lr_agree(const ChainPool& w, ElementId& acc, bool& seen, ElementId e) {
  if (!seen) {
    acc = e;
    seen = true;
  } else if (!lr_same_site(w, acc, e)) {
    acc = kNoElement;
  }
}

// A pool grows sixteenfold: a pointer into one holds only until it grows.
void lr_room(auto& pool, std::size_t want) {
  if (pool.capacity() >= want) return;
  std::size_t c = pool.capacity() < 16 ? 16 : pool.capacity();
  while (c < want) c *= 16;
  pool.reserve(c);
}

// Append a row under the same growth policy as every graph pool.
void lr_append(auto& pool, const auto& row) {
  lr_room(pool, pool.size() + row.size());
  pool.insert(pool.end(), row.begin(), row.end());
}

// `v` sorted, each value once.
void lr_sort_unique(auto& v) {
  std::ranges::sort(v);
  v.erase(std::unique(v.begin(), v.end()), v.end());
}

// What `. B` adds, once per nonterminal: the lookahead fixed by a follower or
// inherited from the item. Only the active productions are kept.
struct LrClosEntry {
  std::uint16_t prod = 0;
  DynSet fixed{};
  bool inherit = false;
};

// A packed word set over the item domain, in an arena.
struct LrBitWords {
  std::uint64_t* w = nullptr;
  // The words the set occupies: an enumeration never sweeps the whole width.
  std::size_t lo = 0, hi = 0;

  void init(std::uint64_t* room, std::size_t n) {
    w = room;
    lo = n;
    hi = 0;
  }
  void set(std::size_t i) {
    const std::size_t k = i >> 6;
    w[k] |= std::uint64_t{1} << (i & 63);
    lo = std::min(k, lo);
    hi = std::max(k, hi);
  }
  bool test(std::size_t i) const { return ((w[i >> 6] >> (i & 63)) & 1) != 0; }
  void merge(const LrBitWords& o) {
    for (std::size_t k = o.lo; k <= o.hi; ++k) w[k] |= o.w[k];
    lo = std::min(o.lo, lo);
    if (o.hi > hi && o.lo <= o.hi) hi = o.hi;
  }
};

struct SegHash {
  std::size_t operator()(const std::array<std::uint32_t, 3>& k) const {
    std::uint64_t h = k[0];
    h = hash_mix(h, k[1]);
    return hash_mix(h, k[2]);
  }
};

// A kernel's hash, lookaheads included where the identity keeps them apart.
std::uint64_t lr_kernel_hash(const LrItem* k, std::size_t n, const DynSet* la) {
  std::uint64_t h = 0x243f6a8885a308d3ull;
  for (std::size_t j = 0; j < n; ++j) {
    h = hash_mix(h, (static_cast<std::uint64_t>(k[j].prod) << 16) | k[j].dot);
    if (la)
      for (std::size_t w = 0; w < la[j].w.size(); ++w)
        h = hash_mix(h, la[j].w[w]);
  }
  return h;
}

bool lr_same_core(const LrItem* p, std::size_t n, const LrItem* q,
                  std::size_t m) {
  return n == m && std::equal(p, p + n, q);
}

bool lr_same_full(const LrItem* p, std::size_t n, const LrItem* q,
                  std::size_t m, const DynSet* la, const DynSet* lb) {
  return lr_same_core(p, n, q, m) && std::equal(la, la + n, lb);
}

// A row of actions ordered on the terminal, stably, by counting over the
// mask of the terminals it holds. `cnt` is left cleared.
void lr_sort_on_term(LrAction* row, std::size_t n, const DynSet& mask,
                     std::uint32_t* cnt, LrAction* tmp) {
  std::uint32_t off = 0;
  each_bit(mask.w.data(), mask.w.size(), [&](std::size_t t) {
    const std::uint32_t c = cnt[t];
    cnt[t] = off;
    off += c;
  });
  for (std::size_t i = 0; i < n; ++i) tmp[i] = row[i];
  for (std::size_t i = 0; i < n; ++i) row[cnt[tmp[i].term]++] = tmp[i];
  each_bit(mask.w.data(), mask.w.size(), [&](std::size_t t) { cnt[t] = 0; });
}

// Dense, and ordered by (production, dot).
struct LrItemDomain {
  std::vector<std::uint32_t> base;  // production -> its item at dot 0
  std::vector<std::uint16_t> prodOf;
  std::size_t n = 0;
  std::size_t words = 0;

  LrItem item(std::size_t ix) const {
    const std::uint16_t p = prodOf[ix];
    return {p, static_cast<std::uint16_t>(ix - base[p])};
  }
};

LrItemDomain lr_item_domain(const LrBuildCfg& g) {
  const LrLowProd* const prods = g.prods.data();
  const unsigned nP = g.nprods;
  LrItemDomain d;
  d.base.assign(nP + 1, std::uint32_t{});
  for (unsigned p = 0; p < nP; ++p)
    d.base[p + 1] = d.base[p] + prods[p].len + 1;
  d.n = d.base[nP];
  d.words = (d.n + 63) / 64;
  d.prodOf.assign(d.n, std::uint16_t{});
  for (unsigned p = 0; p < nP; ++p)
    for (unsigned dot = 0; dot <= prods[p].len; ++dot)
      d.prodOf[d.base[p] + dot] = static_cast<std::uint16_t>(p);
  return d;
}

// What `. B` adds for every nonterminal B, and the items it sets as a mask.
struct LrClosures {
  // Each rule's entries, ascending by production.
  std::vector<LrClosEntry> entries;
  std::vector<std::uint32_t> base;
  std::vector<std::uint64_t> maskArena;
  std::vector<LrBitWords> mask;

  std::span<const LrClosEntry> of(unsigned B) const {
    return {entries.data() + base[B], base[B + 1] - base[B]};
  }
};

// Close the productions reachable through each rule's first symbol once.
// The same entries serve canonical lookaheads and the order a cell forks in.
LrClosures lr_closures(const LrBuildCfg& g, const LrItemDomain& dom,
                       const LrBuckets<std::uint16_t>& pn,
                       const LrFirstSets& fs) {
  LrClosures cl{{},
                std::vector<std::uint32_t>(g.nnts + 1),
                std::vector<std::uint64_t>(dom.words * g.nnts),
                std::vector<LrBitWords>(g.nnts)};
  for (unsigned B = 0; B < g.nnts; ++B)
    cl.mask[B].init(cl.maskArena.data() + dom.words * B, dom.words);
  std::vector<LrClosEntry> row(g.nprods);
  std::vector<char> live(g.nprods), queued(g.nprods);
  std::vector<std::uint16_t> active, todo;
  DynSet endOnly;
  endOnly.set(kEofSymbol);
  for (unsigned B = 0; B < g.nnts; ++B) {
    for (const auto p : active) {
      live[p] = 0;
      row[p].fixed.w.clear();
      row[p].inherit = false;
    }
    active.clear();
    const auto add = [&](std::uint16_t p, const DynSet& reads, bool inherit) {
      const bool ends = g.prodBuild[p].endOnly;
      const DynSet& fixed = ends ? endOnly : reads;
      inherit = inherit && !ends;
      LrClosEntry& e = row[p];
      bool changed = !live[p] || (inherit && !e.inherit);
      if (!live[p]) {
        live[p] = 1;
        active.push_back(p);
        e.prod = p;
      }
      e.inherit = e.inherit || inherit;
      changed = e.fixed.add(fixed) || changed;
      if (changed && !queued[p]) {
        queued[p] = 1;
        todo.push_back(p);
      }
    };
    for (const std::uint16_t p : pn.of(B)) add(p, {}, true);
    DynSet fixed;
    while (!todo.empty()) {
      const auto p = todo.back();
      todo.pop_back();
      queued[p] = 0;
      const LrLowProd& pr = g.prods[p];
      if (pr.len == 0 || is_term(g.sym_at(pr, 0))) continue;
      fixed.w.clear();
      bool inherit = true;
      for (unsigned i = 1; i < pr.len && inherit; ++i) {
        const auto sym = g.sym_at(pr, i);
        fs.add(fixed, sym);
        inherit = !is_term(sym) && fs.nullable[nt_index(sym)];
      }
      if (inherit) fixed |= row[p].fixed;
      inherit = inherit && row[p].inherit;
      for (const auto q : pn.of(nt_index(g.sym_at(pr, 0))))
        add(q, fixed, inherit);
    }
    std::sort(active.begin(), active.end());
    cl.base[B] = static_cast<std::uint32_t>(cl.entries.size());
    for (const auto p : active) {
      cl.entries.push_back(row[p]);
      cl.mask[B].set(dom.base[p]);
    }
  }
  cl.base[g.nnts] = static_cast<std::uint32_t>(cl.entries.size());
  return cl;
}

// One kind of transition, kept once for graph walks and final table rows.
struct LrEdges {
  std::vector<LrEdge> pool;
  std::vector<std::uint32_t> at, len;

  std::span<const LrEdge> operator[](unsigned s) const {
    return {pool.data() + at[s], len[s]};
  }
  void append(unsigned s, std::span<const LrEdge> row) {
    if (at.size() <= s) {
      at.resize(s + 1);
      len.resize(s + 1);
    }
    at[s] = static_cast<std::uint32_t>(pool.size());
    len[s] = static_cast<std::uint32_t>(row.size());
    lr_append(pool, row);
  }
};

// The canonical graph: each state's kernel, closure and edges, in pools.
struct LrGraph {
  std::vector<LrItem> kernPool;
  std::vector<DynSet> kernLaPool;  // empty under `core`
  std::vector<std::uint32_t> kernAt, kernLen;
  std::vector<LrItem> closPool;
  // A lookahead per closure item, zeroed under `core` for the propagation.
  std::vector<DynSet> closLaPool;
  std::vector<std::uint32_t> closAt, closLen;
  LrEdges shifts, gotos;
  // What a state remembers of the gap its span opened on: the conditions it
  // knows and those that held. Part of its identity.
  std::vector<DynSet> tagKnown, tagTruth;
  std::vector<LrGuard> progs;

  unsigned states() const { return static_cast<unsigned>(kernAt.size()); }
  std::span<const LrItem> kernel(unsigned s) const {
    return {kernPool.data() + kernAt[s], kernLen[s]};
  }
  LrPoolRows<LrItem> clos() const {
    return {closPool.data(), closAt.data(), closLen.data()};
  }
  std::span<const LrEdge> edges(unsigned s, std::uint16_t sym) const {
    const auto row = is_term(sym) ? shifts[s] : gotos[s];
    const auto run = std::ranges::equal_range(row, sym, {}, &LrEdge::sym);
    return {run.begin(), run.end()};
  }
  std::vector<std::vector<std::uint32_t>> predecessors() const {
    std::vector<std::vector<std::uint32_t>> preds(states());
    for (unsigned s = 0; s < states(); ++s)
      for (const LrEdges* row : {&shifts, &gotos})
        for (const LrEdge& e : (*row)[s])
          if (preds[e.to].empty() || preds[e.to].back() != s)
            preds[e.to].push_back(s);
    return preds;
  }
};

// The states `depth` edges below `root`, each once per layer. Stamps and
// predecessor order belong to the caller; only its final row may be sorted.
template <class Stamp>
std::vector<std::uint32_t> lr_back(
    const std::vector<std::vector<std::uint32_t>>& preds,
    std::vector<Stamp>& seen, Stamp& gen, unsigned root, unsigned depth) {
  std::vector<std::uint32_t> here{root}, next;
  for (unsigned i = 0; i < depth; ++i) {
    next.clear();
    ++gen;
    for (const std::uint32_t x : here)
      for (const std::uint32_t y : preds[x])
        if (seen[y] != gen) {
          seen[y] = gen;
          next.push_back(y);
        }
    here.swap(next);
  }
  return here;
}

// The graph walked from the start kernel; `core` keys a state on its core.
template <bool WithLa>
LrGraph lr_graph(LrBuildTable& A, const LrItemDomain& dom,
                 const LrFirstSets& fs, const LrClosures& cl) {
  const auto& g = A.cfg;
  const ChainPool& writes = A.writes;
  const LrLowProd* const prods = g.prods.data();
  const unsigned nT = g.nterms;
  const unsigned nN = g.nnts;
  const unsigned nP = g.nprods;
  const LrClosEntry* const closFlat = cl.entries.data();
  LrGraph gr;

  // Scratch rows cleared by the enumeration that reads them, never swept.
  LrBitWords seen;
  std::vector<std::uint64_t> seenWords(dom.words, std::uint64_t{});
  seen.init(seenWords.data(), dom.words);
  std::vector<DynSet> laS(WithLa ? dom.n : 0);

  std::vector<LrItem> harv(dom.n);
  std::vector<DynSet> harvLa(WithLa ? dom.n : 0);
  auto harvest = [&]() -> std::uint32_t {
    std::uint32_t out = 0;
    const std::size_t klo = seen.lo, khi = seen.hi;
    seen.lo = dom.words;
    seen.hi = 0;
    for (std::size_t k = klo; k <= khi && k < dom.words; ++k) {
      std::uint64_t bits = seen.w[k];
      seen.w[k] = 0;
      while (bits) {
        const std::size_t i =
            (k << 6) | static_cast<unsigned>(std::countr_zero(bits));
        bits &= bits - 1;
        const std::uint16_t p = dom.prodOf[i];
        harv[out] = LrItem{p, static_cast<std::uint16_t>(i - dom.base[p])};
        if (WithLa) {
          harvLa[out] = laS[i];
          laS[i] = DynSet{};
        }
        ++out;
      }
    }
    return out;
  };

  // The lookahead a call site hands its callee, gathered per nonterminal:
  // both unions are idempotent, so one walk of its closure list serves all.
  std::vector<DynSet> ntLa(WithLa ? nN : 0);
  std::vector<char> ntMark(nN, char{});
  std::vector<unsigned> ntList(nN, unsigned{});
  std::uint32_t nNtList = 0;

  auto closure = [&](const LrItem* kernel, std::size_t kn, const DynSet* kla) {
    for (std::size_t j = 0; j < kn; ++j) {
      const LrItem& it = kernel[j];
      const std::size_t i = dom.base[it.prod] + it.dot;
      seen.set(i);
      if (WithLa) laS[i] |= kla[j];
    }
    nNtList = 0;
    for (std::size_t j = 0; j < kn; ++j) {
      const LrItem& it = kernel[j];
      const unsigned B = g.nt_after(it.prod, it.dot);
      if (B == kNoRow) continue;
      if (!ntMark[B]) {
        ntMark[B] = 1;
        if (WithLa) ntLa[B] = DynSet{};
        ntList[nNtList++] = B;
        seen.merge(cl.mask[B]);
      }
      if (WithLa) ntLa[B] |= fs.after(it.prod, it.dot + 1, kla[j]);
    }
    for (std::uint32_t z = 0; z < nNtList; ++z) {
      const unsigned B = ntList[z];
      if (WithLa) {
        const DynSet& s = ntLa[B];
        for (const LrClosEntry& e : cl.of(B)) {
          DynSet& d = laS[dom.base[e.prod]];
          d |= e.fixed;
          if (e.inherit) d |= s;
        }
      }
      ntMark[B] = 0;
    }
    return harvest();
  };

  auto& [kernPool, kernLaPool, kernAt, kernLen, closPool, closLaPool, closAt,
         closLen, shifts, gotos, tagKnown, tagTruth, progs] = gr;
  // A start and not a bound: a real graph runs past it.
  const std::size_t poolHint = dom.n * 4;
  closPool.reserve(poolHint);
  shifts.pool.reserve(poolHint);
  gotos.pool.reserve(poolHint);
  kernPool.reserve(poolHint);
  if (WithLa) {
    closLaPool.reserve(poolHint);
    kernLaPool.reserve(poolHint);
  }
  ChainIndex kernIx;
  kernIx.init(nP + 1);

  {
    DynSet la0;
    la0.set(kEofSymbol);
    kernAt.push_back(0);
    kernLen.push_back(1);
    kernPool.push_back(LrItem{g.startProd, 0});
    if (WithLa) kernLaPool.push_back(la0);
    tagKnown.emplace_back();
    tagTruth.emplace_back();
    kernIx.add(lr_kernel_hash(kernPool.data(), 1,
                              WithLa ? kernLaPool.data() : nullptr),
               0);
  }

  // A state is its kernel and its tag.
  auto find_or_add = [&](std::uint32_t n, bool& fresh, const DynSet& known,
                         const DynSet& held) -> unsigned {
    std::uint64_t h =
        lr_kernel_hash(harv.data(), n, WithLa ? harvLa.data() : nullptr);
    for (const std::uint64_t x : known.w) h = hash_mix(h, x);
    for (const std::uint64_t x : held.w) h = hash_mix(h, x);
    const LrItem* const kp = kernPool.data();
    const DynSet* const klp = WithLa ? kernLaPool.data() : nullptr;
    for (unsigned i = kernIx.first(h); i != ChainIndex::kNone;
         i = kernIx.after(i)) {
      if (!(tagKnown[i] == known) || !(tagTruth[i] == held)) continue;
      // Under `core` a kernel is its core: lookaheads are propagated later.
      if (WithLa ? lr_same_full(kp + kernAt[i], kernLen[i], harv.data(), n,
                                klp + kernAt[i], harvLa.data())
                 : lr_same_core(kp + kernAt[i], kernLen[i], harv.data(), n)) {
        fresh = false;
        return i;
      }
    }
    const unsigned id = static_cast<unsigned>(kernAt.size());
    if (id >= 0xffffu) {
      A.cap_hit(
          "it needs more than 65 535 parser states (optional items and "
          "counted repetitions multiply them, and n conditions on one token "
          "make 2^n)");
      fresh = false;
      return 0;
    }
    const std::size_t kb = kernPool.size();
    kernAt.push_back(static_cast<std::uint32_t>(kb));
    kernLen.push_back(n);
    lr_append(kernPool, std::span(harv).first(n));
    if (WithLa) lr_append(kernLaPool, std::span(harvLa).first(n));
    tagKnown.push_back(known);
    tagTruth.push_back(held);
    kernIx.add(h, id);
    fresh = true;
    return id;
  };

  // `req[ci]` is what closure item `ci` must see hold to advance: its own
  // guard, and for an item the closure opened, what one call of its rule
  // passed. `left[n]` is the symbols a span of `n` opens on.
  const bool guarded =
      std::any_of(g.slotGuard.data(), g.slotGuard.data() + g.nslots,
                  [](std::uint8_t k) { return k != 0; });
  std::vector<LrReq> req(guarded ? dom.n : 0);
  std::vector<DynSet> own(guarded ? dom.n : 0);
  std::vector<LrReq> gamma(guarded ? nN : 0);
  std::vector<std::uint32_t> gammaGen(guarded ? nN : 0, std::uint32_t{});
  std::vector<std::uint32_t> kernGen(guarded ? dom.n : 0, std::uint32_t{});
  std::uint32_t visitGen = 0;
  std::vector<std::uint32_t> openers;
  std::vector<DynSet> left(guarded ? nN : 0);
  for (unsigned n = 0; n < left.size(); ++n) {
    left[n].set(nT + n);
    for (const LrClosEntry& e : cl.of(n)) {
      const LrLowProd& q = prods[e.prod];
      if (q.len == 0) continue;
      const std::uint16_t x = g.sym_at(q, 0);
      left[n].set(is_term(x) ? x : nT + nt_index(x));
    }
  }
  const DynSet noTag;
  std::vector<LrEdge> edgeRow;

  // A state reached again is a state already done.
  std::vector<unsigned> work{0u};
  std::vector<ElementId> entrySite(nN);
  // The call that names a rule outright: it outranks a transitive reading.
  std::vector<ElementId> nearSite(nN);
  std::vector<std::uint32_t> entrySeen(nN, std::uint32_t{});
  std::vector<std::uint32_t> nearSeen(nN, std::uint32_t{});
  std::uint32_t gen = 0;
  std::vector<std::uint16_t> ntSyms(nN, std::uint16_t{});
  std::uint32_t nNtSyms = 0;
  // Items bucketed by the symbol after the dot, each bucket in closure order.
  const std::size_t nSym = static_cast<std::size_t>(nT) + nN;
  std::vector<std::uint32_t> symAt(nSym + 1);
  std::vector<std::uint32_t> symCnt(nSym, std::uint32_t{});
  std::vector<std::uint32_t> symFill(nSym);
  std::vector<std::uint32_t> symItem(dom.n);
  while (!work.empty() && !A.overflow) {
    unsigned s = work.back();
    work.pop_back();
    // Sized before the visit: `find_or_add` grows the pools under it.
    if (closAt.size() < kernAt.size()) {
      closAt.resize(kernAt.size(), 0);
      closLen.resize(kernAt.size(), 0);
    }
    {
      const std::uint32_t kb = kernAt[s];
      const std::uint32_t c0n =
          closure(kernPool.data() + kb, kernLen[s],
                  WithLa ? kernLaPool.data() + kb : nullptr);
      const std::size_t cb = closPool.size();
      closAt[s] = static_cast<std::uint32_t>(cb);
      closLen[s] = c0n;
      lr_append(closPool, std::span(harv).first(c0n));
      if (WithLa) lr_append(closLaPool, std::span(harvLa).first(c0n));
    }
    const LrItem* const cs = closPool.data() + closAt[s];
    const DynSet* const csLa = WithLa ? closLaPool.data() + closAt[s] : nullptr;
    const std::uint32_t csn = closLen[s];

    // A rule's requirement gathers its calls', each its own guard joined to
    // each way into its rule: a fixpoint. A kernel item owes its own alone.
    bool gated = false;
    for (std::uint32_t ci = 0; guarded && ci < csn && !gated; ++ci) {
      const LrLowProd& p = prods[cs[ci].prod];
      gated = cs[ci].dot < p.len && g.guard_at(p, cs[ci].dot) != 0;
    }
    openers.clear();
    if (gated) {
      ++visitGen;
      for (std::uint32_t j = 0; j < kernLen[s]; ++j) {
        const LrItem& k = kernPool[kernAt[s] + j];
        kernGen[dom.base[k.prod] + k.dot] = visitGen;
      }
      const auto opened = [&](std::uint32_t ci) {
        return kernGen[dom.base[cs[ci].prod] + cs[ci].dot] != visitGen;
      };
      for (std::uint32_t ci = 0; ci < csn; ++ci) {
        const LrLowProd& p = prods[cs[ci].prod];
        own[ci] = DynSet{};
        if (cs[ci].dot < p.len && g.guard_at(p, cs[ci].dot) != 0)
          own[ci].set(g.guard_at(p, cs[ci].dot) - 1u);
      }
      for (bool changed = true; changed;) {
        changed = false;
        for (std::uint32_t ci = 0; ci < csn; ++ci) {
          const LrLowProd& p = prods[cs[ci].prod];
          const unsigned B = g.nt_after(cs[ci].prod, cs[ci].dot);
          if (B == kNoRow) continue;
          if (opened(ci) && gammaGen[p.lhs] != visitGen) continue;
          if (gammaGen[B] != visitGen) {
            gammaGen[B] = visitGen;
            gamma[B].clear();
          }
          if (!opened(ci)) {
            changed |= lr_req_add(gamma[B], own[ci]);
            continue;
          }
          // By index: a rule that calls itself grows the list it reads.
          for (std::size_t k = 0; k < gamma[p.lhs].size(); ++k)
            changed |= lr_req_add(gamma[B], gamma[p.lhs][k] | own[ci]);
        }
      }
      for (std::uint32_t ci = 0; ci < csn; ++ci) {
        const LrLowProd& p = prods[cs[ci].prod];
        req[ci].clear();
        if (opened(ci) && gammaGen[p.lhs] == visitGen) {
          for (const DynSet& a : gamma[p.lhs]) lr_req_add(req[ci], a | own[ci]);
        } else {
          req[ci].push_back(own[ci]);
        }
        if (lr_req_asks(req[ci]) &&
            g.nt_after(cs[ci].prod, cs[ci].dot) != kNoRow)
          openers.push_back(ci);
      }
    }

    // One pass over the closure: the symbols the state meets, and how many
    // items each advances.
    nNtSyms = 0;
    DynSet actMask;
    for (std::uint32_t ci = 0; ci < csn; ++ci) {
      const LrItem& it = cs[ci];
      const LrLowProd& p = prods[it.prod];
      if (it.dot >= p.len) continue;
      const std::uint16_t x = g.sym_at(p, it.dot);
      if (is_term(x)) {
        actMask.set(x);
        ++symCnt[x];
        continue;
      }
      // The count is the dedup.
      if (++symCnt[nT + nt_index(x)] == 1) ntSyms[nNtSyms++] = x;
    }
    // The call each nonterminal was entered through, among the calls `kept`
    // leaves.
    const auto entered = [&](auto&& kept) {
      ++gen;
      for (std::uint32_t ci = 0; ci < csn; ++ci) {
        const LrItem& it = cs[ci];
        const LrLowProd& p = prods[it.prod];
        const unsigned Bx = g.nt_after(it.prod, it.dot);
        if (Bx == kNoRow || !kept(ci)) continue;
        const ElementId row = g.row_at(p, it.dot);
        // The nearest site: the slot's chain, or a unit production's own
        // where the call and its caller each lay a record.
        const ElementId near =
            row != kNoElement || p.len != 1 ? row : ElementId{p.node};
        if (near != kNoElement) {
          bool seen = std::exchange(nearSeen[Bx], gen) == gen;
          lr_agree(writes, nearSite[Bx], seen, near);
        }
        if (row == kNoElement) continue;
        for (std::uint32_t ei = cl.base[Bx], ee = cl.base[Bx + 1]; ei < ee;
             ++ei) {
          const unsigned y = prods[closFlat[ei].prod].lhs;
          bool seen = std::exchange(entrySeen[y], gen) == gen;
          lr_agree(writes, entrySite[y], seen, row);
        }
      }
    };
    entered([](std::uint32_t) { return true; });
    std::uint32_t symTop = 0;
    // The terminals the state meets, ascending, then its nonterminals.
    const auto lay = [&](std::size_t k) {
      symAt[k] = symFill[k] = symTop;
      symTop += symCnt[k];
      symCnt[k] = 0;
    };
    each_bit(actMask.w.data(), actMask.w.size(), lay);
    for (std::uint32_t q = 0; q < nNtSyms; ++q) lay(nT + nt_index(ntSyms[q]));
    for (std::uint32_t j = 0; j < csn; ++j) {
      const LrItem& it = cs[j];
      const LrLowProd& p = prods[it.prod];
      if (it.dot >= p.len) continue;
      const std::uint16_t x = g.sym_at(p, it.dot);
      symItem[symFill[is_term(x) ? x : nT + nt_index(x)]++] = j;
    }

    // The edges on one symbol: one landing, or where guards decide, one per
    // outcome of the conditions the symbol asks -- its items' guards, and
    // those of the calls whose span it may open.
    edgeRow.clear();
    const auto edges_on = [&](std::size_t key, std::uint16_t sym) {
      const bool terminal = is_term(sym);
      const std::uint32_t k0 = symAt[key], k1 = symFill[key];
      const auto advance = [&](std::uint32_t k, ElementId& site, bool& seenSite,
                               ElementId& outer, bool& seenOuter) {
        const LrItem& it = cs[symItem[k]];
        const LrLowProd& p = prods[it.prod];
        const std::size_t ni = dom.base[it.prod] + it.dot + 1;
        seen.set(ni);
        if (WithLa) laS[ni] |= csLa[symItem[k]];
        if (!terminal) return;
        const ElementId row = g.row_at(p, it.dot);
        lr_agree(writes, site, seenSite, row);
        // The outer member is named where the rule opens, and there only: an
        // item at its start owes the site it was entered through.
        const ElementId ent = nearSeen[p.lhs] == gen    ? nearSite[p.lhs]
                              : entrySeen[p.lhs] == gen ? entrySite[p.lhs]
                                                        : kNoElement;
        lr_agree(writes, outer, seenOuter, it.dot != 0 ? row : ent);
      };
      const auto opens = [&](std::uint32_t y) {
        return left[nt_index(g.sym_at(prods[cs[y].prod], cs[y].dot))].test(
            static_cast<unsigned>(key));
      };
      DynSet ask;
      if (gated) {
        for (std::uint32_t k = k0; k < k1; ++k)
          lr_req_conds(req[symItem[k]], ask);
        for (const std::uint32_t y : openers)
          if (opens(y)) lr_req_conds(req[y], ask);
      }
      if (ask.none()) {
        ElementId site = kNoElement, outer = kNoElement;
        bool seenSite = false, seenOuter = false;
        for (std::uint32_t k = k0; k < k1; ++k)
          advance(k, site, seenSite, outer, seenOuter);
        bool fresh = false;
        const unsigned t = find_or_add(harvest(), fresh, noTag, noTag);
        edgeRow.push_back(
            LrEdge{sym, static_cast<std::uint16_t>(t), site, outer});
        if (fresh) work.push_back(t);
        return;
      }
      LrGuard prog;
      each_bit(ask.w.data(), ask.w.size(), [&](std::size_t c) {
        prog.conds.push_back(static_cast<std::uint8_t>(c));
      });
      // An outcome per subset of the conditions.
      if (prog.conds.size() > 16) {
        A.cap_hit("more than 16 conditions apply to one token");
        return;
      }
      if (progs.size() >= 0xffffu) {
        A.cap_hit("it needs more than 65 535 sets of conditions");
        return;
      }
      const std::size_t nOut = std::size_t{1} << prog.conds.size();
      prog.to.assign(nOut, LrGuard::kNoState);
      prog.site.assign(nOut, kNoElement);
      prog.outer.assign(nOut, kNoElement);
      const auto progId = static_cast<std::uint32_t>(progs.size());
      const std::size_t rowAt = edgeRow.size();
      for (std::size_t v = 0; v < nOut; ++v) {
        DynSet truth;
        for (std::size_t b = 0; b < prog.conds.size(); ++b)
          if ((v >> b) & 1u) truth.set(prog.conds[b]);
        const auto kept = [&](std::uint32_t ci) {
          return lr_req_holds(req[ci], truth);
        };
        if (terminal && !openers.empty()) entered(kept);
        ElementId site = kNoElement, outer = kNoElement;
        bool seenSite = false, seenOuter = false, any = false;
        for (std::uint32_t k = k0; k < k1; ++k)
          if (kept(symItem[k])) {
            advance(k, site, seenSite, outer, seenOuter);
            any = true;
          }
        if (!any) continue;
        // A call's guard is remembered where some item its span may start on
        // went through without it.
        DynSet known;
        for (const std::uint32_t y : openers) {
          if (!opens(y)) continue;
          const unsigned n = nt_index(g.sym_at(prods[cs[y].prod], cs[y].dot));
          for (std::uint32_t k = k0; k < k1; ++k) {
            const std::uint32_t ci = symItem[k];
            if (!kept(ci)) continue;
            const bool starts =
                (key == nT + n && ci == y) ||
                (cs[ci].dot == 0 && cl.mask[n].test(dom.base[cs[ci].prod]));
            if (starts && !lr_req_implies(req[ci], req[y])) {
              lr_req_conds(req[y], known);
              break;
            }
          }
        }
        bool fresh = false;
        const unsigned t = find_or_add(harvest(), fresh, known, truth & known);
        prog.to[v] = static_cast<std::uint16_t>(t);
        prog.site[v] = site;
        prog.outer[v] = outer;
        if (fresh) work.push_back(t);
        if (std::none_of(edgeRow.begin() + static_cast<std::ptrdiff_t>(rowAt),
                         edgeRow.end(),
                         [&](const LrEdge& e) { return e.to == t; }))
          edgeRow.push_back(LrEdge{sym, static_cast<std::uint16_t>(t), site,
                                   outer, progId + 1u});
      }
      progs.push_back(std::move(prog));
      if (terminal && !openers.empty())
        entered([](std::uint32_t) { return true; });
    };
    each_bit(actMask.w.data(), actMask.w.size(), [&](std::size_t a) {
      edges_on(a, static_cast<std::uint16_t>(a));
    });
    const auto split = edgeRow.size();
    for (std::uint32_t q = 0; q < nNtSyms; ++q)
      edges_on(nT + nt_index(ntSyms[q]), ntSyms[q]);
    const std::span<const LrEdge> row = edgeRow;
    shifts.append(s, row.first(split));
    gotos.append(s, row.subspan(split));
  }
  if (A.overflow) return gr;

  const unsigned ns = gr.states();
  closAt.resize(ns, 0);
  closLen.resize(ns, 0);
  if (!WithLa) closLaPool.resize(closPool.size());
  // Terminal rows are already sorted. Gotos are ordered by (state, symbol):
  // their pool indices are also the lookback transition identities.
  auto unordered = std::move(gotos.pool);
  gotos.pool.reserve(unordered.size());
  for (unsigned s = 0; s < ns; ++s) {
    LrEdge* const row = unordered.data() + gotos.at[s];
    std::ranges::stable_sort(std::span(row, gotos.len[s]), {}, &LrEdge::sym);
    gotos.at[s] = static_cast<std::uint32_t>(gotos.pool.size());
    gotos.pool.insert(gotos.pool.end(), row, row + gotos.len[s]);
  }
  return gr;
}

// What each reduction looks back to, kept for the conflicts laid later.
struct LrLookback {
  std::vector<std::uint32_t> from, origin;
  std::vector<std::uint16_t> prod;
  std::vector<DynSet> follow, read;
  // The transitions whose lookahead a transition takes in: `inclBy.of(d)`
  // lists the pairs of `d`, `inclSrc` their sources.
  std::vector<std::uint32_t> inclSrc;
  LrBuckets<> inclBy;
  // The lookbacks of each state, ordered on (production, transition).
  LrBuckets<> byState;

  // Whether the merge may have made a conflict. Where both readings are
  // called in one state that reads the token, every canonical copy holds
  // it: not induced. Anywhere else the wider graph decides.
  bool merge_induced(unsigned st, std::uint16_t p1, std::uint16_t p2,
                     std::uint16_t t) const {
    const std::span<const std::uint32_t> row = byState.of(st);
    const auto run = [&](std::uint16_t p) {
      return std::ranges::equal_range(row, p, {},
                                      [&](std::uint32_t x) { return prod[x]; });
    };
    const auto hands = [&](std::uint32_t d) {
      return d != 0xffffffffu && follow[d].test(t);
    };
    const auto r2 = run(p2);
    bool any = false;
    for (const std::uint32_t q : run(p1)) {
      const std::uint32_t d = from[q];
      if (!hands(d)) continue;
      any = true;
      // A run ascends on the transition, and so on the state it leaves.
      const std::uint32_t o = origin[q];
      auto at = std::ranges::lower_bound(
          r2, o, {}, [&](std::uint32_t x) { return origin[x]; });
      for (; at != r2.end() && origin[*at] == o; ++at) {
        const std::uint32_t d2 = from[*at];
        if (!hands(d2)) continue;
        if (d2 == d || read[d].test(t) || read[d2].test(t)) return false;
      }
    }
    // No transition handed it the token: no context to keep apart.
    return any;
  }

  // The terminals state `st` offers that read over a terminal one of its
  // callers takes: the merged row asked against each caller's own lookahead
  // beside the shifts.
  DynSet lex_flagged(unsigned st, const DynSet& shifted,
                     LexOvertakes<LrBuildCfg>& overtakes) const {
    const std::span<const std::uint32_t> row = byState.of(st);
    struct Run {
      std::vector<DynSet> parts;
      DynSet all;
    };
    std::vector<Run> runs;
    for (std::size_t i = 0, j; i < row.size(); i = j) {
      Run run;
      const auto add = [&](const DynSet& x) {
        if (x.none()) return;
        run.all |= x;
        if (std::ranges::find(run.parts, x) == run.parts.end())
          run.parts.push_back(x);
      };
      for (j = i; j < row.size() && prod[row[j]] == prod[row[i]]; ++j) {
        const std::uint32_t d = from[row[j]];
        if (d == 0xffffffffu) continue;
        add(read[d]);
        for (const std::uint32_t k : inclBy.of(d)) add(follow[inclSrc[k]]);
      }
      runs.push_back(std::move(run));
    }
    DynSet merged = shifted;
    for (const Run& run : runs) merged |= run.all;
    DynSet flagged;
    for (const Run& run : runs)
      if (run.parts.size() > 1)
        for (const DynSet& part : run.parts)
          flagged |= overtakes.overtaking(merged, part | shifted);
    return flagged;
  }

  // Whether a reduce of state `st` hands the implied tokens `implied` to its
  // callers unevenly, with more than one among them: merged, the row answers
  // one a caller refuses.
  bool implied_mixed(unsigned st, const DynSet& implied) const {
    const std::span<const std::uint32_t> row = byState.of(st);
    const auto handed = [&](std::uint32_t q) {
      return follow[from[q]] & implied;
    };
    for (std::size_t i = 0, j; i < row.size(); i = j) {
      DynSet all;
      for (j = i; j < row.size() && prod[row[j]] == prod[row[i]]; ++j)
        if (from[row[j]] != 0xffffffffu) all |= handed(row[j]);
      if (!all.more_than_one()) continue;
      for (std::size_t k = i; k < j; ++k)
        if (from[row[k]] != 0xffffffffu && handed(row[k]) != all) return true;
    }
    return false;
  }
};

// DeRemer-Pennello lookaheads over the goto graph. The lookback relation
// says whether the merge put two readings in one cell, asked of every
// identity; only `writeLa` writes the sets.
LrLookback lr_lookaheads(const LrBuildCfg& g, LrGraph& gr,
                         const LrFirstSets& fs, bool writeLa) {
  const LrLowProd* const prods = g.prods.data();
  const unsigned nP = g.nprods;
  const unsigned ns = gr.states();
  const LrPoolRows<LrItem> clos = gr.clos();
  const auto& gt = gr.gotos;
  const auto ntr = static_cast<std::uint32_t>(gt.pool.size());
  LrLookback lb;
  std::vector<DynSet> read(ntr);
  for (std::uint32_t d = 0; d < ntr; ++d)
    for (const LrEdge& e : gr.shifts[gt.pool[d].to]) read[d].set(e.sym);
  // The goto rows of the state last asked, by nonterminal.
  std::vector<std::uint32_t> trAsked(g.nnts, 0);
  std::vector<std::pair<std::uint32_t, std::uint32_t>> trRow(g.nnts);
  const auto trIds = [&](unsigned p, std::uint16_t nt) {
    if (trAsked[nt] != p + 1u) {
      trAsked[nt] = p + 1u;
      const auto row = gr.edges(p, nt_sym(nt));
      const auto at = static_cast<std::uint32_t>(row.data() - gt.pool.data());
      trRow[nt] = {at, at + static_cast<std::uint32_t>(row.size())};
    }
    return trRow[nt];
  };

  // The entry is followed by end of input, which acceptance never shifts.
  if (const std::uint16_t b = g.nt_after(g.startProd, 0);
      b != kNoRow && prods[g.startProd].len == 1) {
    const auto [e0, e1] = trIds(0, b);
    for (std::uint32_t e = e0; e < e1; ++e) read[e].set(kEofSymbol);
  }

  // The goto that closes a span: the landing its guard picks off the tag of
  // the state the span's first step landed in.
  const auto closing = [&](std::uint32_t s0, std::uint32_t s1,
                           unsigned first) -> std::uint32_t {
    if (s0 == s1 || gt.pool[s0].prog == 0) return s0 == s1 ? 0xffffffffu : s0;
    const LrGuard& pg = gr.progs[gt.pool[s0].prog - 1u];
    std::size_t v = 0;
    for (std::size_t b = 0; b < pg.conds.size(); ++b)
      if (!gr.tagKnown[first].test(pg.conds[b]) ||
          gr.tagTruth[first].test(pg.conds[b]))
        v |= std::size_t{1} << b;
    for (std::uint32_t e = s0; e < s1; ++e)
      if (gt.pool[e].to == pg.to[v]) return e;
    return 0xffffffffu;
  };

  // The fixpoint of one relation: `v[into[k]]` holds `v[from[k]]`.
  const auto close_over = [&](std::vector<DynSet>& v,
                              const std::vector<std::uint32_t>& into,
                              const std::vector<std::uint32_t>& from) {
    const LrBuckets<> out(ntr, static_cast<std::uint32_t>(from.size()),
                          [&](std::uint32_t k) { return from[k]; });
    std::vector<char> queued(ntr, char{1});
    std::vector<std::uint32_t> fw(ntr);
    std::iota(fw.begin(), fw.end(), 0u);
    std::uint32_t nfw = ntr;
    while (nfw != 0) {
      const std::uint32_t d = fw[--nfw];
      queued[d] = 0;
      for (const std::uint32_t k : out.of(d)) {
        const std::uint32_t up = into[k];
        if (v[up].add(v[d]) && !queued[up]) {
          queued[up] = 1;
          fw[nfw++] = up;
        }
      }
    }
  };

  // `reads`: past a nullable nonterminal, the next transition's reads.
  {
    std::vector<std::uint32_t> into, from;
    for (std::uint32_t d = 0; d < ntr; ++d)
      for (const LrEdge& e : gt[gt.pool[d].to]) {
        if (fs.first[nt_index(e.sym)].test(kEofSymbol)) read[d].set(kEofSymbol);
        if (fs.nullable[nt_index(e.sym)]) {
          into.push_back(d);
          from.push_back(static_cast<std::uint32_t>(&e - gt.pool.data()));
        }
      }
    close_over(read, into, from);
  }

  // `includes` and `lookback`, read off one walk of a right-hand side from
  // the state its rule was called in.
  unsigned maxLen = 1;
  std::vector<std::uint32_t> tailEdges(nP, 0);
  for (unsigned q = 0; q < nP; ++q) {
    maxLen = std::max(prods[q].len + 1u, maxLen);
    for (unsigned i = prods[q].len; i-- > 0;) {
      const std::uint16_t y = g.sym_at(prods[q], i);
      if (is_term(y)) break;
      ++tailEdges[q];
      if (!fs.nullable[nt_index(y)]) break;
    }
  }
  std::size_t lbCap = 0, inclCap = 0;
  for (const LrItem& it : gr.closPool)
    if (it.dot == 0) {
      ++lbCap;
      inclCap += tailEdges[it.prod];
    }
  std::vector<std::uint32_t> lbState;
  lbState.reserve(lbCap);
  lb.prod.reserve(lbCap);
  lb.from.reserve(lbCap);
  lb.origin.reserve(lbCap);
  std::vector<std::uint32_t> inclDst, inclSrc;
  inclDst.reserve(inclCap);
  inclSrc.reserve(inclCap);
  // A walk takes every landing a guard may pick; each path is closed by the
  // goto its first landing picks, an empty rule by every one.
  std::vector<unsigned> path(maxLen, unsigned{});
  std::vector<std::span<const LrEdge>> step(maxLen);
  const auto close_path = [&](unsigned p, std::uint16_t prod) {
    const LrLowProd& pr = prods[prod];
    const auto [s0, s1] = trIds(p, pr.lhs);
    const auto record = [&](std::uint32_t src) {
      lbState.push_back(path[pr.len]);
      lb.prod.push_back(prod);
      lb.from.push_back(src);
      lb.origin.push_back(p);
      if (src == 0xffffffffu) return;
      for (unsigned i = pr.len; i-- > 0;) {
        const std::uint16_t y = g.sym_at(pr, i);
        if (is_term(y)) break;
        const auto dst =
            static_cast<std::uint32_t>(step[i].data() - gt.pool.data());
        if (g.prodBuild[prod].endOnly)
          read[dst].set(kEofSymbol);
        else {
          inclDst.push_back(dst);
          inclSrc.push_back(src);
        }
        if (!fs.nullable[nt_index(y)]) break;
      }
    };
    if (pr.len != 0) {
      record(closing(s0, s1, path[1]));
    } else if (s0 == s1) {
      record(0xffffffffu);
    } else {
      for (std::uint32_t e = s0; e < s1; ++e) record(e);
    }
  };
  std::vector<std::uint32_t> openAsked(std::size_t{g.nterms} + g.nnts, 0);
  std::vector<std::span<const LrEdge>> openRow(openAsked.size());
  for (unsigned p = 0; p < ns; ++p)
    for (const LrItem& it : clos[p]) {
      if (it.dot != 0) continue;
      const LrLowProd& pr = prods[it.prod];
      path[0] = p;
      if (pr.len == 0) {
        close_path(p, it.prod);
        continue;
      }
      const std::uint16_t first = g.sym_at(pr, 0);
      const std::size_t slot =
          is_term(first) ? first : std::size_t{g.nterms} + nt_index(first);
      if (openAsked[slot] != p + 1u) {
        openAsked[slot] = p + 1u;
        openRow[slot] = gr.edges(p, first);
      }
      step[0] = openRow[slot];
      for (unsigned i = 0;;) {
        if (step[i].empty()) {
          if (i == 0) break;
          --i;
          step[i] = step[i].subspan(1);
          continue;
        }
        path[i + 1] = step[i].front().to;
        if (i + 1 == pr.len) {
          close_path(p, it.prod);
          step[i] = step[i].subspan(1);
          continue;
        }
        ++i;
        step[i] = gr.edges(path[i], g.sym_at(pr, i));
      }
    }
  const auto nLb = static_cast<std::uint32_t>(lbState.size());

  std::vector<DynSet> follow = read;
  close_over(follow, inclDst, inclSrc);

  // Written into the room the graph left empty.
  DynSet* const closLa = gr.closLaPool.data();
  for (std::uint32_t q = 0; q < nLb && writeLa; ++q) {
    DynSet in;
    if (lb.prod[q] == g.startProd)
      in.set(kEofSymbol);  // the accept owes end of input, nothing else
    else if (lb.from[q] != 0xffffffffu)
      in = follow[lb.from[q]];
    else
      continue;
    const std::uint16_t pd = lb.prod[q];
    const std::uint16_t dot = prods[pd].len;
    for (std::uint32_t ci = clos.at[lbState[q]], ce = ci + clos.len[lbState[q]];
         ci < ce; ++ci)
      if (clos.items[ci].prod == pd && clos.items[ci].dot == dot) {
        closLa[ci] |= in;
        break;
      }
  }
  lb.follow = std::move(follow);
  lb.read = std::move(read);
  lb.inclBy = LrBuckets<>(ntr, static_cast<std::uint32_t>(inclDst.size()),
                          [&](std::uint32_t k) { return inclDst[k]; });
  lb.inclSrc = std::move(inclSrc);
  lb.byState =
      LrBuckets<>(ns, nLb, [&](std::uint32_t q) { return lbState[q]; });
  for (std::uint32_t st = 0; st < ns; ++st)
    std::sort(lb.byState.item.begin() + lb.byState.base[st],
              lb.byState.item.begin() + lb.byState.base[st + 1],
              [&](std::uint32_t x, std::uint32_t y) {
                return lb.prod[x] != lb.prod[y] ? lb.prod[x] < lb.prod[y]
                                                : lb.from[x] < lb.from[y];
              });
  return lb;
}

// Actions are read from transitions and completed items. Only re-fusion
// needs a sorted cache; the final merge consumes this view directly.
struct LrActions {
  const LrBuildCfg& g;
  const LrGraph& gr;
  unsigned dedentTerm = 0;

  LrActions(const LrBuildCfg& g, const LrGraph& gr) : g(g), gr(gr) {
    // An end assertion holds at a dedent too: the last line may end without
    // a newline.
    for (unsigned t = 1; t < g.nterms; ++t)
      if (g.terms[t].laid == kLaidDedent) dedentTerm = t;
  }

  template <class F>
  void each(unsigned s, F&& f) const {
    const auto row = gr.shifts[s];
    for (std::size_t k = 0; k < row.size(); ++k) {
      const LrEdge& e = row[k];
      LrAction a{e.sym, e.prog == 0 ? LrActionKind::shift : LrActionKind::guard,
                 static_cast<std::uint16_t>(e.prog == 0 ? e.to : e.prog - 1u),
                 e.site, e.outer};
      // One guarded terminal is one action. Its landings name one caret
      // hint, or none.
      while (e.prog != 0 && k + 1 < row.size() && row[k + 1].sym == e.sym) {
        const LrEdge& next = row[++k];
        if (a.site != next.site) a.site = kNoElement;
        if (a.outer != next.outer) a.outer = kNoElement;
      }
      f(a);
    }
    for (std::uint32_t ci = gr.closAt[s], ce = ci + gr.closLen[s]; ci < ce;
         ++ci) {
      const LrItem& it = gr.closPool[ci];
      if (it.dot != g.prods[it.prod].len) continue;
      const DynSet& la = gr.closLaPool[ci];
      const std::uint16_t stop = g.prodBuild[it.prod].stop;
      each_bit(la.w.data(), la.w.size(), [&](std::size_t t) {
        if (t >= g.nterms) return;
        if (stop != 0 && g.stopCuts[stop - 1u].test(static_cast<unsigned>(t)))
          return;
        if (g.prodBuild[it.prod].endOnly && t != kEofSymbol &&
            !(dedentTerm != 0 && t == dedentTerm))
          return;
        const auto term = static_cast<std::uint16_t>(t);
        f((it.prod == g.startProd && t == kEofSymbol)
              ? LrAction{term, LrActionKind::accept, 0}
              : LrAction{term, LrActionKind::reduce, it.prod});
      });
    }
  }
};

// Per state, the terminals its row offers that the lexer may read over one a
// caller of the state takes.
std::vector<DynSet> lr_lex_flags(const LrGraph& gr, const LrLookback& lb,
                                 LexOvertakes<LrBuildCfg>& overtakes) {
  std::vector<DynSet> flagged(gr.states());
  for (unsigned s = 0; s < gr.states(); ++s) {
    DynSet shifted;
    for (const LrEdge& e : gr.shifts[s]) shifted.set(e.sym);
    flagged[s] = lb.lex_flagged(s, shifted, overtakes);
  }
  return flagged;
}

// The same for the groups of canonical states the re-fusion made: the rows of
// a group's members are what its callers offer apart.
std::vector<DynSet> lr_group_flags(const LrGraph& gr, const LrActions& actions,
                                   const std::vector<unsigned>& group,
                                   unsigned ngroups,
                                   LexOvertakes<LrBuildCfg>& overtakes) {
  std::vector<DynSet> merged(ngroups);
  std::vector<std::vector<DynSet>> parts(ngroups);
  for (unsigned s = 0; s < gr.states(); ++s) {
    DynSet row;
    actions.each(s, [&](const LrAction& a) { row.set(a.term); });
    merged[group[s]] |= row;
    if (std::ranges::find(parts[group[s]], row) == parts[group[s]].end())
      parts[group[s]].push_back(std::move(row));
  }
  std::vector<DynSet> flagged(ngroups);
  for (unsigned f = 0; f < ngroups; ++f)
    if (parts[f].size() > 1)
      for (const DynSet& part : parts[f])
        flagged[f] |= overtakes.overtaking(merged[f], part);
  return flagged;
}

// The re-fusion: states of one core form a group, separated only where they
// conflict or answer different implied tokens, more than one between them;
// targets agree when they land in the same group, a fixpoint.
std::vector<unsigned> lr_refuse(const LrGraph& gr, const LrActions& actions) {
  const unsigned ns = gr.states();
  std::vector<LrAction> acts, tmp;
  std::vector<std::uint32_t> base(ns + 1), count(actions.g.nterms);
  std::vector<DynSet> implied(ns);
  for (unsigned s = 0; s < ns; ++s) {
    DynSet mask;
    actions.each(s, [&](const LrAction& a) {
      acts.push_back(a);
      ++count[a.term];
      mask.set(a.term);
      if (actions.g.terms[a.term].laid == kLaidImplied) implied[s].set(a.term);
    });
    base[s + 1] = static_cast<std::uint32_t>(acts.size());
    const auto n = base[s + 1] - base[s];
    if (tmp.size() < n) tmp.resize(n);
    lr_sort_on_term(acts.data() + base[s], n, mask, count.data(), tmp.data());
  }
  const auto row = [&](unsigned s) -> std::span<const LrAction> {
    return {acts.data() + base[s], base[s + 1] - base[s]};
  };
  std::vector<unsigned> grp(ns, 0);
  {
    ChainIndex ix;
    ix.init(ns);
    std::vector<unsigned> repr;  // one canonical core per group
    const LrItem* const kp = gr.kernPool.data();
    // A tag is core.
    for (unsigned s = 0; s < ns; ++s) {
      std::uint64_t h =
          lr_kernel_hash(kp + gr.kernAt[s], gr.kernLen[s], nullptr);
      for (const std::uint64_t x : gr.tagKnown[s].w) h = hash_mix(h, x);
      for (const std::uint64_t x : gr.tagTruth[s].w) h = hash_mix(h, x);
      unsigned found = static_cast<unsigned>(repr.size());
      for (unsigned i = ix.first(h); i != ChainIndex::kNone; i = ix.after(i))
        if (gr.tagKnown[repr[i]] == gr.tagKnown[s] &&
            gr.tagTruth[repr[i]] == gr.tagTruth[s] &&
            lr_same_core(kp + gr.kernAt[repr[i]], gr.kernLen[repr[i]],
                         kp + gr.kernAt[s], gr.kernLen[s])) {
          found = i;
          break;
        }
      if (found == repr.size()) {
        repr.push_back(s);
        ix.add(h, found);
      }
      grp[s] = found;
    }
  }
  const auto same_landings = [&](std::uint16_t a, std::uint16_t b) {
    const LrGuard& x = gr.progs[a];
    const LrGuard& y = gr.progs[b];
    if (x.conds != y.conds) return false;
    for (std::size_t v = 0; v < x.to.size(); ++v)
      if (x.to[v] == LrGuard::kNoState || y.to[v] == LrGuard::kNoState
              ? x.to[v] != y.to[v]
              : grp[x.to[v]] != grp[y.to[v]])
        return false;
    return true;
  };
  // A class the first fit has filled. Its members agree pairwise wherever
  // they both answer, so one of them speaks for the actions of each terminal
  // and for the gotos, and a state fits it when it agrees with those alone.
  struct Class {
    unsigned first = 0;
    std::vector<unsigned> impliedBy;
    struct Speaks {
      std::uint16_t term;
      std::uint32_t at, len;
    };
    std::vector<Speaks> speaks;
  };
  const auto same_run = [&](const LrAction* u, const LrAction* v,
                            std::size_t n) {
    // A run is ordered, so two agree action for action.
    for (std::size_t a = 0; a < n; ++a) {
      if (u[a].kind != v[a].kind) return false;
      if (u[a].kind == LrActionKind::shift ? grp[u[a].arg] != grp[v[a].arg]
          : u[a].kind == LrActionKind::guard
              ? !same_landings(u[a].arg, v[a].arg)
              : u[a].arg != v[a].arg)
        return false;
      // The outer site counts, the caret's call site does not.
      if (u[a].outer != v[a].outer) return false;
    }
    return true;
  };
  const auto fits = [&](const Class& c, unsigned s) {
    for (const unsigned m : c.impliedBy)
      if (implied[s] != implied[m] && (implied[s] | implied[m]).more_than_one())
        return false;
    const auto u = row(s);
    std::size_t i = 0;
    auto at = c.speaks.begin();
    while (i < u.size() && at != c.speaks.end()) {
      if (u[i].term < at->term) {
        ++i;
        continue;
      }
      if (at->term < u[i].term) {
        ++at;
        continue;
      }
      std::size_t i2 = i;
      while (i2 < u.size() && u[i2].term == u[i].term) ++i2;
      if (i2 - i != at->len ||
          !same_run(u.data() + i, acts.data() + at->at, at->len))
        return false;
      i = i2;
      ++at;
    }
    // One core: the two rows of gotos name the same symbols in order.
    const std::span<const LrEdge> e1 = gr.gotos[s], e2 = gr.gotos[c.first];
    assert(e1.size() == e2.size());
    for (std::size_t k = 0; k < e1.size(); ++k)
      if (grp[e1[k].to] != grp[e2[k].to]) return false;
    return true;
  };
  std::vector<Class::Speaks> merged;
  const auto join = [&](Class& c, unsigned s) {
    if (std::none_of(c.impliedBy.begin(), c.impliedBy.end(),
                     [&](unsigned m) { return implied[m] == implied[s]; }))
      c.impliedBy.push_back(s);
    const auto u = row(s);
    merged.clear();
    std::size_t i = 0, j = 0;
    while (i < u.size() || j < c.speaks.size()) {
      if (j == c.speaks.size() ||
          (i < u.size() && u[i].term < c.speaks[j].term)) {
        std::size_t i2 = i;
        while (i2 < u.size() && u[i2].term == u[i].term) ++i2;
        merged.push_back(
            {u[i].term, static_cast<std::uint32_t>(u.data() - acts.data() + i),
             static_cast<std::uint32_t>(i2 - i)});
        i = i2;
        continue;
      }
      if (i < u.size() && u[i].term == c.speaks[j].term)
        while (i < u.size() && u[i].term == c.speaks[j].term) ++i;
      merged.push_back(c.speaks[j++]);
    }
    c.speaks.swap(merged);
  };
  // Each group split greedily in place. A group is re-asked only where a
  // successor left a group that split: the same first-fit remakes it.
  std::vector<Class> classes;
  std::vector<unsigned> ng(ns, unsigned{});
  std::vector<char> dirty(ns + 1, char{1});
  std::vector<char> nextDirty(ns + 1, char{});
  std::vector<char> split(ns + 1, char{});
  std::vector<char> moved(ns, char{});
  for (bool changed = true; changed;) {
    unsigned before = 0;
    for (unsigned s = 0; s < ns; ++s) before = std::max(before, grp[s] + 1);
    const LrBuckets<> m(before, ns, [&](std::uint32_t s) { return grp[s]; });
    const std::uint32_t* const mOf = m.item.data();
    unsigned ngrp = 0;
    for (unsigned gi = 0; gi < before; ++gi) {
      const unsigned b = m.base[gi], e = m.base[gi + 1];
      unsigned made = 0;
      if (!dirty[gi]) {
        for (unsigned i = b; i < e; ++i) ng[mOf[i]] = ngrp;
        made = b < e ? 1u : 0u;
      } else {
        classes.clear();
        for (unsigned i = b; i < e; ++i) {
          const unsigned s = mOf[i];
          unsigned put = made;
          for (unsigned c2 = 0; c2 < made && put == made; ++c2)
            if (fits(classes[c2], s)) put = c2;
          if (put == made) {
            ++made;
            classes.emplace_back();
            classes.back().first = s;
          }
          join(classes[put], s);
          ng[s] = ngrp + put;
        }
      }
      split[gi] = made > 1 ? char{1} : char{0};
      ngrp += made;
    }
    for (unsigned s = 0; s < ns; ++s) moved[s] = split[grp[s]];
    for (unsigned c = 0; c < ngrp; ++c) nextDirty[c] = 0;
    for (unsigned s = 0; s < ns; ++s) {
      if (nextDirty[ng[s]]) continue;
      bool d = false;
      for (const LrAction& x : row(s)) {
        d = d || (x.kind == LrActionKind::shift && moved[x.arg]);
        if (x.kind == LrActionKind::guard)
          for (const std::uint16_t to : gr.progs[x.arg].to)
            d = d || (to != LrGuard::kNoState && moved[to]);
      }
      for (const LrEdge& e : gr.gotos[s]) d = d || moved[e.to];
      if (d) nextDirty[ng[s]] = 1;
    }
    for (unsigned s = 0; s < ns; ++s) grp[s] = ng[s];
    for (unsigned c = 0; c < ngrp; ++c) dirty[c] = nextDirty[c];
    // Refinement only splits: stable iff the count is.
    changed = ngrp != before;
  }
  return grp;
}

// The ways five origins can be alike: for each, the place of the first origin
// it equals.
struct LrAlikes {
  std::array<std::array<std::uint8_t, 5>, 52> of{};
  std::array<std::uint8_t, 1 << 15> id{};
  constexpr LrAlikes() {
    unsigned n = 0;
    for (std::uint8_t d = 0; d < 5; ++d)
      for (std::uint8_t c = 0; c < 4; ++c)
        for (std::uint8_t b = 0; b < 3; ++b)
          for (std::uint8_t a = 0; a < 2; ++a) {
            const std::array<std::uint8_t, 5> like{0, a, b, c, d};
            bool ok = true;
            for (unsigned i = 1; i < 5; ++i)
              ok = ok && like[like[i]] == like[i];
            if (!ok) continue;
            of[n] = like;
            id[a << 3 | b << 6 | c << 9 | d << 12] =
                static_cast<std::uint8_t>(n++);
          }
  }
};
inline constexpr LrAlikes kLrAlikes{};

// The order the grammar wrote, read off the election words: two readings of a
// cell compare as the election compares their derivations, top down in text
// order, from the lowest node their item chains share. A chain is the item and
// the kernel items two levels over it; the left corner between two levels lays
// its least words. Chains may meet at several heights: the highest
// that tells the readings apart decides, as they part there first. The order
// is exact for every text where nothing climbs past those levels but chains
// the other holds, that repeat a node or that end, and the readings meet one
// way at every height.
class LrWrittenOrder {
 public:
  LrWrittenOrder(const LrBuildCfg& g, const LrItemDomain& dom,
                 const LrGraph& gr, const LrClosures& cl,
                 const LrFirstSets& fs, const LrBuckets<std::uint16_t>& pn,
                 const LrBuildTable& A)
      : _g(g), _dom(dom), _gr(gr), _cl(cl), _fs(fs), _pn(pn), _A(A) {}

  // Lets go of the layouts and the forward walks held once they weigh too
  // much; a layout met again is built again, and what its meetings said
  // stands. The layouts of shapes `a` and `b` stay.
  void trim(std::uint32_t a = kNone, std::uint32_t b = kNone) {
    if (_links.size() >= kMostLinks) _links.clear();
    if (_held < kMostHeld) return;
    _held = 0;
    for (std::uint32_t i = 0; i < _layouts.size(); ++i) {
      Layout& lay = _layouts[i];
      if ((a != kNone && i == _shapes[a].layout) ||
          (b != kNone && i == _shapes[b].layout)) {
        _held += lay.weight;
        continue;
      }
      Layout fresh;
      fresh.data = std::move(lay.data);
      fresh.gen = lay.gen + 1;
      lay = std::move(fresh);
    }
    for (Mid& md : _midPool) {
      md.parents = {};
      md.parentOff = {};
    }
  }

  // How action `a` of canonical state `s` on `t` stands to `b`: -1 first, +1
  // after, 0 where nothing written tells them apart; and whether every text
  // reads them so.
  struct Decision {
    int verdict = 0;
    bool exact = false;
  };
  Decision operator()(unsigned s, const LrAction& a, const LrAction& b,
                      unsigned t) {
    open();
    return decide(s, reading(s, a, t), reading(s, b, t));
  }

 private:
  // The tables every comparison reads, laid at the first one: a graph with no
  // cell to order lays none.
  void open() {
    if (!_keys.empty()) return;
    _keys.emplace_back();
    _rows.resize(_gr.states());
    _opens.resize(_g.nnts);
    _corner.resize(_g.nnts);
    _preds = _gr.predecessors();
    _seen.assign(_gr.states(), 0);
    _bucketOf.assign(_dom.n, kNone);
    _restEmpty.assign(_dom.n, 1);
    for (unsigned p = 0; p < _g.nprods; ++p)
      for (unsigned i = _g.prods[p].len; i-- > 0;) {
        const std::uint16_t y = _g.sym_at(_g.prods[p], i);
        _restEmpty[_dom.base[p] + i] = !is_term(y) &&
                                       _fs.nullable[nt_index(y)] &&
                                       _restEmpty[_dom.base[p] + i + 1];
      }
    _nexts.resize(_gr.states());
    _nextsMade.assign(_gr.states(), 0);
    for (unsigned s = 0; s < _gr.states(); ++s)
      for (const LrItem& k : _gr.kernel(s))
        _maxDot = std::max<std::uint32_t>(_maxDot, k.dot);
    _reach.resize(_maxDot + 2u);
    _climbOf.assign(std::size_t{_g.nprods} * _g.nterms, kNone);
  }

  static constexpr std::size_t kMostHeld = std::size_t{1} << 26;
  static constexpr std::size_t kMostLinks = std::size_t{1} << 22;
  static constexpr std::uint32_t kNone = ~std::uint32_t{0};

  // What a chain is but for where it stands: its items from the top down, and
  // the words the chain lays, laid when a say first reads them.
  struct Skeleton {
    std::array<LrItem, 3> at{};
    std::uint32_t seq = kNone;
    std::uint8_t len = 0;
  };
  // A state of a chain of three.
  struct Ent {
    std::uint64_t ab = 0;
    std::uint32_t chain = 0;
  };
  // The states of the chains of three that share a root offset, a production
  // and a dot of their tops, which `key` holds in that order.
  struct Run {
    std::uint64_t key = 0;
    std::uint32_t begin = 0, end = 0;
  };
  // A chain over a root of a list. `from..to` hold the states its top's dot may
  // stand in; a chain of two shares in `over` the record of the nodes it may
  // climb to. A chain of one has no states: the state of the readings is the
  // meeting's.
  struct Chain {
    std::uint32_t height = 0, from = 0, to = 0, over = 0;
    // The chain of two a chain of three climbs over.
    std::uint32_t mid = 0;
    LrItem top;
    // For a chain of three, the production under the top, and how far under the
    // top the root stands: two dots, whose sum may pass sixteen bits.
    std::uint16_t next = 0, root = 0;
    std::uint32_t rootOff = 0;
    std::uint8_t len = 1;
  };
  // What a root climbs through: the nodes it may begin in and, for each, the
  // kernel items over it that the token it was read for allows.
  struct Rise {
    std::uint32_t origins = 0, dot = 0;
    bool narrowed = false;
    std::vector<std::uint64_t> flat;
  };
  // The nodes a chain of two may climb to, by the state its top begins in, then
  // the state its dot stands in: the same for every list whose chain over the
  // same states has this top. `parents` holds, by dot, the kernel items over
  // those nodes and the place in `over` of each; `parentOff` bounds each dot's
  // run, and is empty until they are laid.
  struct Mid {
    std::vector<std::uint64_t> key, over, parents;
    std::vector<std::uint32_t> parentOff;
    LrItem up;
  };
  // What stands at a height of a layout, laid when asked.
  struct Stand {
    bool shortMade = false, tallMade = false;
    std::vector<std::uint64_t> shorts, talls;
  };
  // The chains of the lists that climb alike over the same states. The chains
  // of three are laid a height at a time, as the meetings ask.
  struct Layout {
    bool built = false;
    std::uint32_t high = 0, low = 0, gen = 0;
    std::size_t weight = 0;
    std::vector<std::uint32_t> data, rootChain;
    std::vector<Chain> chains;
    std::vector<std::uint64_t> states;
    // The chains of two, by height then item; by height, those and all the
    // chains of three laid, ordered by production, dot and number.
    std::vector<std::uint32_t> mids;
    std::vector<std::vector<std::uint64_t>> shortAt, at;
    std::vector<std::uint8_t> topped;
    // By height, the states of the chains of three, run by run, each by state.
    std::vector<std::vector<Ent>> talls;
    std::vector<std::vector<Run>> runs;
    std::vector<Stand> stand;
    // For a height and an item, the states the chains of two begin in.
    std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> begins;
  };
  // A list: its roots over a layout. Roots that climb alike are of one class
  // of the layout, whose `members` it buckets; `rootSk` holds the skeleton of
  // each, laid when a meeting first asks.
  struct Shape {
    std::uint32_t layout = 0;
    bool narrowed = false;
    std::vector<LrItem> roots;
    std::vector<std::uint16_t> cls;
    LrBuckets<std::uint16_t> members;
    std::vector<std::uint32_t> rootSk;
  };
  struct Meeting;
  // What a join says each pair of chains for as it meets them: the shapes they
  // stand over and the meeting that takes it.
  struct Live {
    Shape* a;
    const Layout* la;
    Shape* b;
    const Layout* lb;
    Meeting* at;
  };
  // Two chains whose tops are one node, and the ways the five origins that
  // stand under the pair were met alike, one bit each.
  struct Meet {
    std::uint32_t x = 0, y = 0;
    std::uint64_t ways = 0;
  };
  // What two layouts meet in at each height: laid when a meeting first asks.
  struct Pair {
    std::uint32_t genA = 0, genB = 0;
    std::vector<std::vector<Meet>> at;
    std::vector<std::uint8_t> done;
  };
  // The items a shape climbs from, and the terminal the first step over a
  // reduced one reads.
  struct List {
    std::vector<LrItem> roots;
    std::uint32_t t = kNone;
  };
  // What a list lays past a shape: nothing for a shift, the words a reduced
  // production owes, the least words of what a kernel item opens for a
  // reading that begins here; one tail per root.
  struct Reading {
    std::uint32_t list = kNone;
    std::uint32_t tailAt = 0;
  };
  // Two derivations that end on one node, with nothing under it that they
  // share, and say nothing as far as the shorter goes: left to the tails.
  struct Tie {
    std::uint16_t xRoot = 0, yRoot = 0;
    std::uint32_t x = 0, y = 0;
  };
  // What the derivations ending at one height say: the direction their
  // bodies settle, and the ties left to the tails.
  struct Meeting {
    bool lower = false, upper = false;
    std::vector<Tie> ties;
  };
  // The meetings of two shapes from the highest height down to the one that
  // settles them; what every height read says (some first, some second, some
  // left to the tails); whether the shapes hold every meeting.
  struct Meetings {
    std::vector<Meeting> list;
    bool lower = false, upper = false, complete = false, tied = false;
  };
  // What two derivations say as far as their bodies go: -1 the first, 1 the
  // second, 0 nothing, 2 the same, whose words are `x` and `y`.
  struct Say {
    std::int8_t kind = 0;
    std::uint32_t x = 0, y = 0;
  };
  // A word, or the subtree of the frame at a depth.
  struct Elem {
    std::uint32_t v = 0;
    bool frame = false;
    bool operator==(const Elem&) const = default;
  };
  // What a canonical state shifts on each terminal.
  struct Row {
    bool built = false, shifted = false;
    // The terminals it shifts, ascending, and the rank of each among them plus
    // one, zero for the others.
    std::vector<std::uint16_t> terms;
    std::vector<std::uint16_t> slot;
    std::vector<Reading> shifts;
    std::vector<List> lists;
    std::vector<std::uint32_t> shape;
  };
  // What an item opening nonterminal `b` lays to read a terminal first.
  struct Open {
    std::uint16_t term = 0;
    std::uint32_t tail = 0;
  };
  struct Opens {
    bool built = false;
    std::vector<Open> rows;
  };
  struct Next {
    LrItem item;
    std::uint16_t nt = 0;
  };

  template <class Out>
  void words_at(unsigned p, std::uint16_t at, Out&& out) const {
    if (_A.electAt.empty()) return;
    for (std::uint32_t i = _A.electAt[p]; i < _A.electAt[p + 1]; ++i)
      if (_A.electPos[i] == at) out(_A.electWord[i]);
  }

  std::uint32_t intern(std::vector<std::uint32_t> key) {
    if (key.empty()) return 0;
    _keys.push_back(std::move(key));
    return static_cast<std::uint32_t>(_keys.size() - 1);
  }

  // The least words a left corner lays from nonterminal `b` down to `c`.
  std::uint32_t corner(unsigned b, unsigned c) {
    std::vector<std::uint32_t>& best = _corner[b];
    if (!best.empty()) return best[c];
    best.assign(_g.nnts, kNone);
    best[b] = 0;
    std::vector<unsigned> todo{b};
    std::vector<std::uint32_t> key;
    while (!todo.empty()) {
      const unsigned x = todo.back();
      todo.pop_back();
      for (const std::uint16_t q : _pn.of(x)) {
        const unsigned y = _g.nt_after(q, 0);
        if (y == kNoRow) continue;
        key = _keys[best[x]];
        words_at(q, 0, [&](std::uint32_t w) { key.push_back(w); });
        if (best[y] != kNone &&
            !std::ranges::lexicographical_compare(key, _keys[best[y]]))
          continue;
        best[y] = intern(key);
        todo.push_back(y);
      }
    }
    return best[c];
  }

  // The terminals nonterminal `b` opens on, each with the least words its
  // productions lay to begin with it.
  const std::vector<Open>& opens(unsigned b) {
    Opens& o = _opens[b];
    if (o.built) return o.rows;
    o.built = true;
    struct Best {
      std::uint16_t term;
      std::vector<std::uint32_t> words;
    };
    std::vector<Best> best;
    std::vector<std::uint32_t> key;
    for (const LrClosEntry& e : _cl.of(b)) {
      const LrLowProd& q = _g.prods[e.prod];
      if (q.len == 0 || !is_term(_g.sym_at(q, 0))) continue;
      const std::uint16_t t = _g.sym_at(q, 0);
      key = _keys[corner(b, q.lhs)];
      words_at(e.prod, 0, [&](std::uint32_t w) { key.push_back(w); });
      const auto at = std::ranges::find(best, t, &Best::term);
      if (at == best.end())
        best.push_back({t, key});
      else if (std::ranges::lexicographical_compare(key, at->words))
        at->words = key;
    }
    std::ranges::sort(best, {}, &Best::term);
    for (const Best& x : best) o.rows.push_back({x.term, intern(x.words)});
    return o.rows;
  }

  Row& row(unsigned s) {
    Row& r = _rows[s];
    if (!r.built) build_row(s, r);
    return r;
  }

  void build_row(unsigned s, Row& r) {
    r.built = true;
    r.slot.assign(_g.nterms, 0);
    const auto shifts = _gr.shifts[s];
    for (std::size_t k = 0; k < shifts.size(); ++k) {
      const std::uint16_t t = shifts[k].sym;
      r.terms.push_back(t);
      r.slot[t] = static_cast<std::uint16_t>(r.terms.size());
      while (shifts[k].prog != 0 && k + 1 < shifts.size() &&
             shifts[k + 1].sym == t)
        ++k;
    }
  }

  // The states `n` frames under `s`, ascending.
  const std::vector<std::uint32_t>& back(unsigned s, unsigned n) {
    const std::uint64_t k = std::uint64_t{s} << 32 | n;
    if (const auto it = _back.find(k); it != _back.end()) return it->second;
    auto here = lr_back(_preds, _seen, _seenGen, s, n);
    std::ranges::sort(here);
    return _back.emplace(k, std::move(here)).first->second;
  }

  // Production `p`'s entry in the closure of rule `b`, if it has one: a rule's
  // entries ascend by production.
  const LrClosEntry* entry(unsigned b, unsigned p) const {
    const std::span<const LrClosEntry> of = _cl.of(b);
    const auto it = std::ranges::lower_bound(of, p, {}, &LrClosEntry::prod);
    return it != of.end() && it->prod == p ? &*it : nullptr;
  }

  // Whether kernel item `k` reads `t` past production `p` it opens: 2 where
  // `t` is its next token, 1 where only what follows the item's rule may be.
  int admits(const LrItem& k, unsigned p, unsigned t) {
    const LrLowProd& pr = _g.prods[k.prod];
    if (k.dot >= pr.len || is_term(_g.sym_at(pr, k.dot))) return 0;
    const LrClosEntry* e = entry(nt_index(_g.sym_at(pr, k.dot)), p);
    if (e == nullptr) return 0;
    if (e->fixed.test(t) || (e->inherit && after(k).test(t))) return 2;
    if (_follow.empty()) _follow = lr_follow(_g, _fs.nullable, _fs.first);
    return e->inherit && _restEmpty[item_ix(k) + 1u] && _follow[pr.lhs].test(t)
               ? 1
               : 0;
  }

  // What a run of positions after the dot of `k` may start with.
  const DynSet& after(const LrItem& k) {
    if (_after.empty()) {
      _after.resize(_dom.n);
      _afterMade.assign(_dom.n, 0);
    }
    const unsigned ix = _dom.base[k.prod] + k.dot + 1u;
    if (!_afterMade[ix]) {
      _after[ix] = _fs.after(k.prod, k.dot + 1u, {});
      _afterMade[ix] = 1;
    }
    return _after[ix];
  }

  // What `admits` says of each item for a production and a token, said once.
  std::vector<std::int8_t>& scores(unsigned p, std::uint32_t t) {
    const auto [at, fresh] = _scoreRows.try_emplace(std::uint64_t{p} << 32 | t);
    if (fresh) at->second.assign(_dom.n, -1);
    return at->second;
  }

  int score(std::vector<std::int8_t>& row, const LrItem& k, unsigned p,
            unsigned t) {
    std::int8_t& at = row[item_ix(k)];
    if (at < 0) at = static_cast<std::int8_t>(admits(k, p, t));
    return at;
  }

  // The kernel items of `o` that stand before a nonterminal, with it.
  const std::vector<Next>& nexts(unsigned o) {
    std::vector<Next>& v = _nexts[o];
    if (_nextsMade[o]) return v;
    _nextsMade[o] = 1;
    for (const LrItem& k : _gr.kernel(o))
      if (const std::uint16_t nt = _g.nt_after(k.prod, k.dot); nt != kNoRow)
        v.push_back({k, nt});
    return v;
  }

  // The kernel items of `o` a node of production `p` stands under.
  template <class F>
  void each_parent(unsigned o, unsigned p, F&& f) {
    const unsigned at = _dom.base[p];
    const std::uint64_t bit = std::uint64_t{1} << (at & 63);
    for (const Next& n : nexts(o))
      if ((_cl.mask[n.nt].w[at >> 6] & bit) != 0) f(n.item);
  }

  bool has_parent(unsigned o, unsigned p) {
    const unsigned at = _dom.base[p];
    const std::uint64_t bit = std::uint64_t{1} << (at & 63);
    for (const Next& n : nexts(o))
      if ((_cl.mask[n.nt].w[at >> 6] & bit) != 0) return true;
    return false;
  }

  // The parents a reading of `t` climbs through from a node, whose `row` of
  // scores is `scores(p, t)`, null where `t` is kNone and nothing is scored:
  // for a reduced one, those that read `t` past it as their next token where
  // one does. True where some parent is dropped.
  template <class F>
  bool each_chosen(std::vector<std::int8_t>* row, unsigned o, unsigned p,
                   std::uint32_t t, F&& f) {
    if (row == nullptr) {
      each_parent(o, p, f);
      return false;
    }
    int most = 0;
    std::size_t all = 0;
    each_parent(o, p, [&](const LrItem& k) {
      most = std::max(most, score(*row, k, p, t));
      ++all;
    });
    if (most == 0) return all != 0;
    std::size_t kept = 0;
    each_parent(o, p, [&](const LrItem& k) {
      if (score(*row, k, p, t) != most) return;
      f(k);
      ++kept;
    });
    return kept != all;
  }

  static std::uint64_t path_key(unsigned s, const LrAction& x, unsigned t) {
    const std::uint64_t id = x.kind == LrActionKind::accept ? 0x3FFFEu : x.arg;
    return std::uint64_t{s} << 34 | std::uint64_t{t} << 18 | id;
  }

  unsigned item_ix(const LrItem& k) const { return _dom.base[k.prod] + k.dot; }

  // Appends the words of `n`, `off` frames under its chain's top, after the
  // least words of the left corner from the item `over` it, where there is
  // one.
  void lay_words(std::vector<Elem>& out, const LrItem& n, std::uint32_t off,
                 const LrItem* over) {
    const LrLowProd& q = _g.prods[n.prod];
    if (over != nullptr) {
      const std::uint32_t lead = corner(
          nt_index(_g.sym_at(_g.prods[over->prod], over->dot)), q.lhs);
      for (const std::uint32_t w : _keys[lead]) out.push_back({w, false});
    }
    // A production's words ascend by the child they stand before.
    const bool any = !_A.electAt.empty();
    std::uint32_t w = any ? _A.electAt[n.prod] : 0;
    const std::uint32_t end = any ? _A.electAt[n.prod + 1] : 0;
    for (unsigned i = 0; i <= n.dot; ++i) {
      for (; w < end && _A.electPos[w] == i; ++w)
        out.push_back({_A.electWord[w], false});
      if (i < n.dot && !is_term(_g.sym_at(q, i)))
        out.push_back({off + i, true});
    }
  }

  // The skeleton of `item` over the one of `below`, or alone.
  std::uint32_t skeleton_of(std::uint32_t below, const LrItem& item) {
    const std::uint64_t key =
        std::uint64_t{below + 1u} << 32 | (_dom.base[item.prod] + item.dot);
    auto [id, fresh] = _skeletonOf.emplace(key);
    if (!fresh) return id;
    id = static_cast<std::uint32_t>(_skeletons.size());
    Skeleton sk;
    sk.at[0] = item;
    sk.len = 1;
    if (below != kNone) {
      const Skeleton under = _skeletons[below];
      sk.len = under.len + 1;
      for (unsigned i = 0; i < under.len; ++i) sk.at[i + 1] = under.at[i];
    }
    _skeletons.push_back(sk);
    return id;
  }

  // The words skeleton `id` lays.
  std::uint32_t seq_of(std::uint32_t id) {
    Skeleton& sk = _skeletons[id];
    if (sk.seq != kNone) return sk.seq;
    _fx.clear();
    std::uint32_t off = 0;
    for (unsigned i = 0; i < sk.len; ++i) {
      lay_words(_fx, sk.at[i], off, i == 0 ? nullptr : &sk.at[i - 1]);
      off += sk.at[i].dot;
    }
    return sk.seq = intern(_fx);
  }

  std::uint32_t intern(const std::vector<Elem>& seq) {
    _seqKey.clear();
    for (const Elem& e : seq)
      _seqKey.push_back(std::uint64_t{e.v} << 1 | e.frame);
    const auto [it, fresh] =
        _seqOf.try_emplace(_seqKey, static_cast<std::uint32_t>(_seqs.size()));
    if (fresh) _seqs.push_back(seq);
    return it->second;
  }

  // Appends the chain of `top` over the chain `parent`, its dots standing in
  // `states`, ascending.
  std::uint32_t add_chain(Layout& sh, std::uint32_t parent, const LrItem& top,
                          std::span<const std::uint64_t> states,
                          std::uint16_t root) {
    Chain c;
    c.top = top;
    c.height = top.dot;
    if (parent != kNone) {
      const Chain& under = sh.chains[parent];
      c.len = static_cast<std::uint8_t>(under.len + 1);
      c.height += under.height;
      if (under.len > 1) {
        c.next = under.top.prod;
        c.rootOff = std::uint32_t{top.dot} + under.top.dot;
        c.mid = parent;
      }
    }
    c.root = root;
    c.from = static_cast<std::uint32_t>(sh.states.size());
    sh.states.insert(sh.states.end(), states.begin(), states.end());
    c.to = static_cast<std::uint32_t>(sh.states.size());
    sh.chains.push_back(c);
    return static_cast<std::uint32_t>(sh.chains.size() - 1);
  }

  // The skeleton of a chain of `b` over its root `i`.
  std::uint32_t skeleton_id(Shape& b, const Layout& lay, const Chain& c,
                            unsigned i) {
    const std::uint32_t root = root_skeleton(b, i);
    if (c.len == 1) return root;
    if (c.len == 2) return skeleton_of(root, c.top);
    return skeleton_of(skeleton_of(root, lay.chains[c.mid].top), c.top);
  }

  std::uint32_t root_skeleton(Shape& b, unsigned i) {
    std::uint32_t& sk = b.rootSk[i];
    if (sk == kNone) sk = skeleton_of(kNone, b.roots[i]);
    return sk;
  }

  // The production under the top of a chain of `b` over its root `i`.
  static std::uint16_t next_of(const Shape& b, const Chain& c, unsigned i) {
    return c.len == 2 ? b.roots[i].prod : c.next;
  }

  // The chains of one and two over the roots of the layout.
  void build(Layout& sh) {
    sh.built = true;
    std::vector<std::uint64_t> ups;
    sh.rootChain.assign(sh.data.size(), kNone);
    for (std::size_t i = 0; i < sh.data.size(); ++i) {
      const Rise& rise = _rises[sh.data[i]];
      if (_origins[rise.origins]->empty()) continue;
      const auto root = static_cast<std::uint16_t>(i);
      const std::uint32_t base = add_chain(
          sh, kNone, LrItem{0, static_cast<std::uint16_t>(rise.dot)}, {}, root);
      sh.rootChain[i] = base;
      const std::vector<std::uint64_t>& flat = rise.flat;
      for (std::size_t from = 0, to = 0; from < flat.size(); from = to) {
        const std::uint32_t ix = static_cast<std::uint32_t>(flat[from] >> 32);
        while (to < flat.size() && (flat[to] >> 32) == ix) ++to;
        ups.clear();
        for (std::size_t k = from; k < to; ++k) ups.push_back(flat[k] << 32);
        const std::uint32_t m = add_chain(sh, base, _dom.item(ix), ups, root);
        sh.chains[m].over = mid_of(std::span(flat).subspan(from, to - from));
        sh.mids.push_back(m);
      }
    }
    if (sh.chains.empty()) return;
    sh.low = sh.high = sh.chains[0].height;
    for (const Chain& c : sh.chains) {
      sh.low = std::min(sh.low, c.height);
      sh.high = std::max(sh.high, c.height);
    }
    sh.high += static_cast<std::uint32_t>(_maxDot);
    std::ranges::sort(sh.mids, [&](std::uint32_t x, std::uint32_t y) {
      const Chain &cx = sh.chains[x], &cy = sh.chains[y];
      if (cx.height != cy.height) return cx.height < cy.height;
      return item_ix(cx.top) < item_ix(cy.top);
    });
    sh.shortAt.assign(sh.high + 1, {});
    sh.at.assign(sh.high + 1, {});
    sh.topped.assign(sh.high + 1, 0);
    sh.talls.assign(sh.high + 1, {});
    sh.runs.assign(sh.high + 1, {});
    sh.stand.assign(sh.high + 1, {});
    for (const std::uint32_t m : sh.mids)
      sh.shortAt[sh.chains[m].height].push_back(
          std::uint64_t{item_ix(sh.chains[m].top)} << 32 | m);
    for (std::uint32_t h = 0; h <= sh.high; ++h) {
      std::ranges::sort(sh.shortAt[h]);
      sh.at[h] = sh.shortAt[h];
    }
    sh.weight = sh.chains.size() * 4 + sh.states.size();
    _held += sh.weight;
  }

  // The record of the nodes a chain of two over the origins in `group` (an item
  // and an origin each, ascending) may climb to.
  std::uint32_t mid_of(std::span<const std::uint64_t> group) {
    std::uint64_t h = 0x4d1d;
    for (const std::uint64_t e : group) h = hash_mix(h, e);
    const auto [lo, hi] = _midIds.equal_range(h);
    for (auto it = lo; it != hi; ++it)
      if (std::ranges::equal(_midPool[it->second].key, group)) return it->second;
    Mid& md = _midPool.emplace_back();
    md.key.assign(group.begin(), group.end());
    md.up = _dom.item(group.front() >> 32);
    for (const std::uint64_t e : group)
      for (const std::uint32_t a : back(static_cast<std::uint32_t>(e), md.up.dot))
        md.over.push_back(std::uint64_t{a} << 32 | static_cast<std::uint32_t>(e));
    std::ranges::sort(md.over);
    const auto id = static_cast<std::uint32_t>(_midPool.size() - 1);
    _midIds.emplace(h, id);
    return id;
  }

  // Lays the kernel items over the nodes of a chain of two.
  void lay_parents(Mid& md) {
    _byDot.resize(_maxDot + 1u);
    for (auto& run : _byDot) run.clear();
    for (std::uint32_t i = 0; i < md.over.size(); ++i)
      each_parent(first_of(md.over[i]), md.up.prod, [&](const LrItem& k) {
        _byDot[k.dot].push_back(std::uint64_t{item_ix(k)} << 32 | i);
      });
    md.parentOff.resize(_maxDot + 2u);
    for (unsigned d = 0; d <= _maxDot; ++d) {
      std::ranges::sort(_byDot[d]);
      md.parentOff[d] = static_cast<std::uint32_t>(md.parents.size());
      md.parents.insert(md.parents.end(), _byDot[d].begin(), _byDot[d].end());
    }
    md.parentOff[_maxDot + 1] = static_cast<std::uint32_t>(md.parents.size());
    _held += md.parents.size();
  }

  // Lays the chains of three whose tops stand at `height`.
  void ensure_tops(Layout& sh, std::uint32_t height) {
    if (height >= sh.topped.size() || sh.topped[height]) return;
    sh.topped[height] = 1;
    std::vector<std::uint32_t>& laid = _laid;
    laid.clear();
    const auto reach = std::ranges::partition_point(
        sh.mids, [&](std::uint32_t m) {
          return sh.chains[m].height + _maxDot < height;
        });
    for (std::size_t mi = reach - sh.mids.begin(); mi < sh.mids.size(); ++mi) {
      const std::uint32_t m = sh.mids[mi];
      const Chain mc = sh.chains[m];
      if (mc.height > height) break;
      Mid& md = _midPool[mc.over];
      if (md.parentOff.empty()) lay_parents(md);
      const std::uint32_t* off = md.parentOff.data();
      const unsigned dot = height - mc.height;
      for (std::uint32_t q = off[dot], r; q < off[dot + 1]; q = r) {
        const auto ix = static_cast<std::uint32_t>(md.parents[q] >> 32);
        _laying.clear();
        for (r = q; r < off[dot + 1] && md.parents[r] >> 32 == ix; ++r)
          _laying.push_back(md.over[static_cast<std::uint32_t>(md.parents[r])]);
        const std::uint32_t c =
            add_chain(sh, m, _dom.item(ix), _laying, mc.root);
        laid.push_back(c);
        sh.weight += 4 + sh.chains[c].to - sh.chains[c].from;
        _held += 4 + sh.chains[c].to - sh.chains[c].from;
      }
    }
    if (laid.empty()) return;
    // The laid chains by item, which `at` takes, then by root offset, which
    // orders the runs of states; each order is stable on the chain.
    _items.clear();
    std::uint32_t most = 0;
    std::size_t total = 0;
    for (const std::uint32_t c : laid) {
      total += sh.chains[c].to - sh.chains[c].from;
      const unsigned ix = item_ix(sh.chains[c].top);
      if (_bucketOf[ix] == kNone) {
        _bucketOf[ix] = 0;
        _items.push_back(ix);
      }
      ++_bucketOf[ix];
      most = std::max<std::uint32_t>(most, sh.chains[c].rootOff);
    }
    std::ranges::sort(_items);
    std::uint32_t place = 0;
    for (const std::uint32_t ix : _items) {
      const std::uint32_t n = _bucketOf[ix];
      _bucketOf[ix] = place;
      place += n;
    }
    _byItem.resize(laid.size());
    for (const std::uint32_t c : laid) {
      const unsigned ix = item_ix(sh.chains[c].top);
      _byItem[_bucketOf[ix]++] = std::uint64_t{ix} << 32 | c;
    }
    for (const std::uint32_t ix : _items) _bucketOf[ix] = kNone;
    std::vector<std::uint64_t>& at = sh.at[height];
    at.resize(sh.shortAt[height].size() + _byItem.size());
    std::ranges::merge(sh.shortAt[height], _byItem, at.begin());
    _offsets.assign(std::size_t{most} + 2, 0);
    for (const std::uint64_t p : _byItem)
      ++_offsets[sh.chains[static_cast<std::uint32_t>(p)].rootOff + 1u];
    for (std::size_t i = 1; i < _offsets.size(); ++i)
      _offsets[i] += _offsets[i - 1];
    _byKey.resize(_byItem.size());
    for (const std::uint64_t p : _byItem) {
      const auto c = static_cast<std::uint32_t>(p);
      _byKey[_offsets[sh.chains[c].rootOff]++] = c;
    }
    std::vector<Ent>& talls = sh.talls[height];
    std::vector<Run>& runs = sh.runs[height];
    talls.reserve(total);
    for (std::size_t i = 0, j; i < _byKey.size(); i = j) {
      const Chain& head = sh.chains[_byKey[i]];
      const std::uint64_t key = std::uint64_t{head.rootOff} << 32 |
                                std::uint64_t{head.top.prod} << 16 |
                                head.top.dot;
      const std::size_t begin = talls.size();
      for (j = i; j < _byKey.size(); ++j) {
        const Chain& ch = sh.chains[_byKey[j]];
        if (ch.rootOff != head.rootOff || ch.top.prod != head.top.prod ||
            ch.top.dot != head.top.dot)
          break;
        for (std::uint32_t k = ch.from; k < ch.to; ++k)
          talls.push_back({sh.states[k], _byKey[j]});
      }
      std::ranges::stable_sort(talls.begin() + begin, talls.end(), {},
                               &Ent::ab);
      runs.push_back({key, static_cast<std::uint32_t>(begin),
                      static_cast<std::uint32_t>(talls.size())});
    }
  }

  void ensure(std::uint32_t id) {
    Layout& sh = _layouts[id];
    if (!sh.built) build(sh);
  }

  // The list of `r` that has these roots and reads `t`.
  std::uint32_t list_of(Row& r, std::span<const LrItem> roots,
                        std::uint32_t t) {
    for (std::uint32_t i = 0; i < r.lists.size(); ++i)
      if (r.lists[i].t == t && std::ranges::equal(r.lists[i].roots, roots))
        return i;
    r.lists.push_back({{roots.begin(), roots.end()}, t});
    r.shape.push_back(kNone);
    return static_cast<std::uint32_t>(r.lists.size() - 1);
  }

  // Every shift reading of a state: the kernel items that read the terminal,
  // or open on it by their least words.
  void build_shifts(unsigned s, Row& r) {
    r.shifted = true;
    r.shifts.assign(r.terms.size(), Reading{});
    const auto kern = _gr.kernel(s);
    const auto each = [&](const LrItem& k, auto&& f) {
      const LrLowProd& pr = _g.prods[k.prod];
      if (k.dot >= pr.len) return;
      const std::uint16_t y = _g.sym_at(pr, k.dot);
      if (is_term(y))
        f(y, std::uint32_t{0});
      else
        for (const Open& o : opens(nt_index(y))) f(o.term, o.tail);
    };
    _first.assign(_g.nterms + 1u, 0);
    for (const LrItem& k : kern)
      each(k, [&](unsigned t, std::uint32_t) { ++_first[t + 1]; });
    for (unsigned t = 0; t < _g.nterms; ++t) _first[t + 1] += _first[t];
    _fill.assign(_first.begin(), _first.end() - 1);
    _csrItem.resize(_first[_g.nterms]);
    _csrTail.resize(_first[_g.nterms]);
    for (const LrItem& k : kern)
      each(k, [&](unsigned t, std::uint32_t tail) {
        _csrItem[_fill[t]] = k;
        _csrTail[_fill[t]++] = tail;
      });
    for (const std::uint16_t t : r.terms) {
      const std::uint32_t b = _first[t], e = _first[t + 1];
      Reading& rd = r.shifts[r.slot[t] - 1u];
      rd.list = list_of(r, std::span(_csrItem).subspan(b, e - b), kNone);
      rd.tailAt = static_cast<std::uint32_t>(_tails.size());
      _tails.insert(_tails.end(), _csrTail.begin() + b, _csrTail.begin() + e);
    }
  }

  // The reading of an action that does not shift.
  Reading generic(unsigned s, const LrAction& x, unsigned t) {
    auto [rd, fresh] = _generic.emplace(path_key(s, x, t));
    if (!fresh) return rd;
    Row& r = row(s);
    rd.tailAt = static_cast<std::uint32_t>(_tails.size());
    if (x.kind == LrActionKind::accept || _g.prods[x.arg].len != 0) {
      // A reduction: the node it completes.
      const unsigned p = x.kind == LrActionKind::accept ? _g.startProd : x.arg;
      const LrItem done{static_cast<std::uint16_t>(p), _g.prods[p].len};
      rd.list = list_of(r, std::span(&done, 1), t);
      _tails.push_back(0);
      return rd;
    }
    // An empty production begins here, under the items that read `t` past it
    // as their next token where one does.
    int most = 0;
    std::vector<LrItem> picked;
    std::vector<std::int8_t>& scored = scores(x.arg, t);
    for (const LrItem& k : _gr.kernel(s)) {
      const int sc = score(scored, k, x.arg, t);
      if (sc > most) {
        most = sc;
        picked.clear();
      }
      if (sc != 0 && sc == most) picked.push_back(k);
    }
    for (const LrItem& k : picked) {
      const LrLowProd& pr = _g.prods[k.prod];
      auto words =
          _keys[corner(nt_index(_g.sym_at(pr, k.dot)), _g.prods[x.arg].lhs)];
      words_at(x.arg, 0, [&](std::uint32_t w) { words.push_back(w); });
      _tails.push_back(intern(std::move(words)));
    }
    rd.list = list_of(r, picked, kNone);
    return rd;
  }

  Reading reading(unsigned s, const LrAction& x, unsigned t) {
    if (!lr_takes_token(x.kind)) return generic(s, x, t);
    Row& r = row(s);
    if (!r.shifted) build_shifts(s, r);
    return r.shifts[r.slot[t] - 1u];
  }

  // The items any state may hold that a node of production `p` stands under.
  const std::vector<LrItem>& parent_items(unsigned p) {
    if (_parentItems.empty()) {
      _parentItems.resize(_g.nprods);
      _parentItemsBuilt.assign(_g.nprods, 0);
    }
    std::vector<LrItem>& out = _parentItems[p];
    if (_parentItemsBuilt[p]) return out;
    _parentItemsBuilt[p] = 1;
    for (unsigned q = 0; q < _g.nprods; ++q)
      for (unsigned d = 0; d < _g.prods[q].len; ++d) {
        const LrLowProd& pr = _g.prods[q];
        if (is_term(_g.sym_at(pr, d))) continue;
        if (_cl.mask[nt_index(_g.sym_at(pr, d))].test(_dom.base[p]))
          out.push_back({static_cast<std::uint16_t>(q),
                         static_cast<std::uint16_t>(d)});
      }
    return out;
  }

  // The class of a token among those that climb through a production alike:
  // the parents it admits, item by item.
  std::uint32_t climb_class(unsigned p, std::uint32_t t) {
    if (t == kNone) return kNone;
    std::uint32_t& known = _climbOf[std::size_t{p} * _g.nterms + t];
    if (known != kNone) return known;
    std::vector<std::uint32_t> id{p};
    std::vector<std::int8_t>& row = scores(p, t);
    for (const LrItem& k : parent_items(p))
      id.push_back(static_cast<std::uint32_t>(score(row, k, p, t)));
    const auto [at, fresh] = _climbs.try_emplace(
        std::move(id), static_cast<std::uint32_t>(_climbs.size()));
    return known = at->second;
  }

  // The id of the states `n` frames under `s`: equal states, equal id. Only
  // the pairs a comparison asks for are held.
  std::uint32_t origins_of(unsigned s, unsigned n) {
    auto [known, fresh] = _originOf.emplace(std::uint64_t{s} << 32 | n);
    if (!fresh) return known;
    const std::vector<std::uint32_t>& v = back(s, n);
    std::uint64_t h = hash_mix(0x51ed270b, v.size());
    for (const std::uint32_t x : v) h = hash_mix(h, x);
    const auto [lo, hi] = _originIds.equal_range(h);
    for (auto it = lo; it != hi; ++it)
      if (*_origins[it->second] == v) return known = it->second;
    const auto id = static_cast<std::uint32_t>(_origins.size());
    _origins.push_back(&v);
    _originIds.emplace(h, id);
    return known = id;
  }

  // What root `r` climbs through over the states `origins` when `t` is the
  // token read; lists that climb alike share it.
  std::uint32_t rise_of(std::uint32_t origins, const LrItem& r,
                        std::uint32_t t) {
    const auto [at, fresh] =
        _riseOf.try_emplace({origins, std::uint32_t{r.prod} << 16 | r.dot, t});
    std::uint32_t& made = at->second;
    if (!fresh) return made;
    Rise rise;
    rise.origins = origins;
    rise.dot = r.dot;
    std::vector<std::int8_t>* const row =
        t == kNone ? nullptr : &scores(r.prod, t);
    for (const std::uint32_t o : *_origins[origins])
      if (each_chosen(row, o, r.prod, t, [&](const LrItem& k) {
            rise.flat.push_back(std::uint64_t{item_ix(k)} << 32 | o);
          }))
        rise.narrowed = true;
    std::ranges::sort(rise.flat);
    std::uint64_t h = hash_mix(hash_mix(0x2545f491, origins), rise.dot);
    h = hash_mix(h, rise.narrowed);
    for (const std::uint64_t x : rise.flat) h = hash_mix(h, x);
    const auto [lo, hi] = _riseIds.equal_range(h);
    for (auto it = lo; it != hi; ++it) {
      const Rise& o = _rises[it->second];
      if (o.origins == origins && o.dot == rise.dot &&
          o.narrowed == rise.narrowed && o.flat == rise.flat)
        return made = it->second;
    }
    const auto id = static_cast<std::uint32_t>(_rises.size());
    _rises.push_back(std::move(rise));
    _riseIds.emplace(h, id);
    return made = id;
  }

  // The shape of a list: lists that climb alike over the same states, from
  // the same roots, have one.
  std::uint32_t shape_of(unsigned s, std::uint32_t list) {
    Row& r = row(s);
    if (r.shape[list] != kNone) return r.shape[list];
    const List& l = r.lists[list];
    std::vector<std::uint32_t> id{s};
    for (const LrItem& k : l.roots)
      id.insert(id.end(), {std::uint32_t{k.prod} << 16 | k.dot,
                           climb_class(k.prod, l.t)});
    if (const auto it = _shapeOf.find(id); it != _shapeOf.end())
      return r.shape[list] = it->second;
    std::vector<std::uint32_t> data, key{0};
    std::vector<std::uint16_t> cls;
    bool narrowed = false;
    for (const LrItem& k : l.roots) {
      const std::uint32_t rise = rise_of(origins_of(s, k.dot), k, l.t);
      narrowed = narrowed || _rises[rise].narrowed;
      const auto at = std::ranges::find(data, rise);
      cls.push_back(static_cast<std::uint16_t>(at - data.begin()));
      if (at == data.end()) data.push_back(rise);
      key.push_back(std::uint32_t{k.prod} << 16 | k.dot);
      key.push_back(cls.back());
    }
    const auto [lay, laid] = _layoutIds.try_emplace(
        data, static_cast<std::uint32_t>(_layouts.size()));
    if (laid) _layouts.emplace_back().data = data;
    key[0] = lay->second;
    const auto [at, fresh] =
        _shapeIds.try_emplace(key, static_cast<std::uint32_t>(_shapes.size()));
    if (fresh) {
      Shape& sh = _shapes.emplace_back();
      sh.layout = lay->second;
      sh.narrowed = narrowed;
      sh.roots = l.roots;
      sh.cls = cls;
      sh.members = {static_cast<std::uint32_t>(data.size()),
                    static_cast<std::uint32_t>(cls.size()),
                    [&](std::uint32_t i) { return cls[i]; }};
      sh.rootSk.assign(l.roots.size(), kNone);
    }
    _shapeOf.emplace(std::move(id), at->second);
    return r.shape[list] = at->second;
  }

  // The meetings of two shapes, worked out once: from the highest height down
  // to the one that settles them; then, while the readings may still be told
  // apart for every text, what the heights below say.
  const Meetings& meetings(unsigned s, std::uint32_t sa, std::uint32_t sb) {
    auto [slot, fresh] = _meetOf.emplace(std::uint64_t{sa} << 32 | sb);
    if (!fresh) return _meetings[slot];
    const auto place = slot = static_cast<std::uint32_t>(_meetings.size());
    _meetings.emplace_back();
    trim(sa, sb);
    ensure(_shapes[sa].layout);
    ensure(_shapes[sb].layout);
    const Layout& A = _layouts[_shapes[sa].layout];
    const Layout& B = _layouts[_shapes[sb].layout];
    Meetings out;
    Meeting at;
    bool settled = false;
    const auto complete = [&] {
      return !_shapes[sa].narrowed && !_shapes[sb].narrowed &&
             closed(s, sa, sb) && closed(s, sb, sa);
    };
    // Takes in the height worked, and clears it.
    const auto close = [&] {
      out.lower = out.lower || at.lower;
      out.upper = out.upper || at.upper;
      const bool open = !at.ties.empty();
      out.tied = out.tied || open;
      if (!settled && (at.lower || at.upper || open)) {
        settled = !open && at.lower != at.upper;
        if (settled) out.complete = complete();
        if (!(at.lower && at.upper)) out.list.push_back(std::move(at));
      }
      at = Meeting{};
      if (!_tieSeen.empty()) _tieSeen.clear();
    };
    const bool any = !A.chains.empty() && !B.chains.empty();
    const std::uint32_t top = std::min(A.high, B.high);
    const std::uint32_t bottom = std::max(A.low, B.low);
    for (std::uint32_t height = top;
         !(settled && (!out.complete || (out.lower && out.upper))) && any &&
         height >= bottom && top >= bottom;
         --height) {
      meet(s, sa, sb, height, at);
      close();
      if (height == 0) break;
    }
    if (!settled) out.complete = complete();
    _meetings[place] = std::move(out);
    return _meetings[place];
  }

  // What the chains of shapes `sa` and `sb` whose tops are one node at
  // `height` say.
  void meet(unsigned s, std::uint32_t sa, std::uint32_t sb,
            std::uint32_t height, Meeting& at) {
    Shape& a = _shapes[sa];
    Shape& b = _shapes[sb];
    Layout& A = _layouts[a.layout];
    Layout& B = _layouts[b.layout];
    Pair& p = pair_of(a.layout, b.layout);
    if (p.at.size() <= height) {
      p.at.resize(height + 1);
      p.done.resize(height + 1, 0);
    }
    if (p.done[height]) {
      for (const Meet& m : p.at[height])
        if (incident(a, A, b, B, m, at)) return;
    } else {
      ensure_tops(A, height);
      ensure_tops(B, height);
      Live live{&a, &A, &b, &B, &at};
      p.at[height].clear();
      _sink = &p.at[height];
      _live = &live;
      _meetAt.reset();
      p.done[height] = join_meets(A, B, height);
      _sink = nullptr;
      _live = nullptr;
      if (!p.done[height]) {
        p.at[height].clear();
        return;
      }
    }
    meet_roots(s, a, b, height, at);
  }

  // What two layouts meet in, made anew where either was built again.
  Pair& pair_of(std::uint32_t la, std::uint32_t lb) {
    Pair& p = _pairs[std::uint64_t{la} << 32 | lb];
    if (p.genA != _layouts[la].gen || p.genB != _layouts[lb].gen) {
      p = Pair{};
      p.genA = _layouts[la].gen;
      p.genB = _layouts[lb].gen;
    }
    return p;
  }

  // The pairs of chains of two and three of two layouts whose tops are one
  // node at `height`, and the ways their states stand, in `_sink`; `_live`
  // says each as it is met, and may stop the join: False where it did.
  bool join_meets(const Layout& A, const Layout& B, std::uint32_t height) {
    for (const std::uint64_t packed : A.shortAt[height]) {
      const Chain& ca = A.chains[static_cast<std::uint32_t>(packed)];
      const auto [first, last] = with_prod(B.at[height], ca.top.prod);
      for (auto it = first; it != last; ++it)
        if (pair_up(A, ca, B, B.chains[static_cast<std::uint32_t>(*it)]))
          return false;
    }
    for (const std::uint64_t packed : B.shortAt[height]) {
      const Chain& cb = B.chains[static_cast<std::uint32_t>(packed)];
      const auto [first, last] = with_prod(A.at[height], cb.top.prod);
      for (auto it = first; it != last; ++it) {
        const Chain& ca = A.chains[static_cast<std::uint32_t>(*it)];
        if (ca.len == 3 && pair_up(A, ca, B, cb)) return false;
      }
    }
    const std::vector<Run>& xs = A.runs[height];
    const std::vector<Run>& ys = B.runs[height];
    for (std::size_t i = 0, ie; i < xs.size(); i = ie) {
      const std::uint64_t rx = xs[i].key >> 32;
      ie = i;
      while (ie < xs.size() && xs[ie].key >> 32 == rx) ++ie;
      for (std::size_t j = 0, je; j < ys.size(); j = je) {
        const std::uint64_t ry = ys[j].key >> 32;
        je = j;
        while (je < ys.size() && ys[je].key >> 32 == ry) ++je;
        if (join_class(A, B, A.talls[height], std::span(xs).subspan(i, ie - i),
                       B.talls[height], std::span(ys).subspan(j, je - j),
                       rx == ry))
          return false;
      }
    }
    return true;
  }

  // What the roots of the shapes, whose chains of one hold the state of the
  // readings, say where their tops are one node at `height`. True where the
  // height is split both ways.
  bool meet_roots(unsigned s, Shape& a, Shape& b, std::uint32_t height,
                  Meeting& at) {
    Layout& A = _layouts[a.layout];
    Layout& B = _layouts[b.layout];
    const std::uint64_t home = std::uint64_t{s} << 32;
    const std::span<const std::uint64_t> here(&home, 1);
    const auto states = [](const Layout& lay, const Chain& c) {
      return std::span<const std::uint64_t>(lay.states)
          .subspan(c.from, c.to - c.from);
    };
    for (std::size_t i = 0; i < a.roots.size(); ++i) {
      if (A.rootChain[a.cls[i]] == kNone || a.roots[i].dot != height) continue;
      const Chain& ra = A.chains[A.rootChain[a.cls[i]]];
      const unsigned p = a.roots[i].prod;
      ensure_tops(B, height);
      const auto [first, last] = with_prod(B.at[height], p);
      for (auto it = first; it != last; ++it) {
        const Chain& cb = B.chains[static_cast<std::uint32_t>(*it)];
        for (const std::uint16_t j : b.members.of(cb.root))
          if (each_state(p, height, false, here, cb.top.dot, cb.len > 1,
                         states(B, cb),
                         [&](std::uint64_t ex, std::uint64_t ey) {
                           return incident(a, A, ra, i, b, B, cb, j, ex, ey, at);
                         }))
            return true;
      }
      for (std::size_t j = 0; j < b.roots.size(); ++j) {
        if (B.rootChain[b.cls[j]] == kNone || b.roots[j].dot != height ||
            b.roots[j].prod != p)
          continue;
        const Chain& rb = B.chains[B.rootChain[b.cls[j]]];
        if (each_state(p, height, false, here, height, false, here,
                       [&](std::uint64_t ex, std::uint64_t ey) {
                         return incident(a, A, ra, i, b, B, rb, j, ex, ey, at);
                       }))
          return true;
      }
    }
    for (std::size_t j = 0; j < b.roots.size(); ++j) {
      if (B.rootChain[b.cls[j]] == kNone || b.roots[j].dot != height) continue;
      const Chain& rb = B.chains[B.rootChain[b.cls[j]]];
      const unsigned p = b.roots[j].prod;
      ensure_tops(A, height);
      const auto [first, last] = with_prod(A.at[height], p);
      for (auto it = first; it != last; ++it) {
        const Chain& ca = A.chains[static_cast<std::uint32_t>(*it)];
        for (const std::uint16_t i : a.members.of(ca.root))
          if (each_state(p, ca.top.dot, ca.len > 1, states(A, ca), height,
                         false, here,
                         [&](std::uint64_t ex, std::uint64_t ey) {
                           return incident(a, A, ca, i, b, B, rb, j, ex, ey, at);
                         }))
            return true;
      }
    }
    return false;
  }

  // The states in which a node of production `p` has its dot after `d2`
  // symbols, when `a` is the state after `d1` of them.
  const std::vector<std::uint32_t>& link(unsigned p, unsigned d1, unsigned d2,
                                         std::uint32_t a) {
    const auto [at, fresh] = _links.try_emplace({p, d1 << 16 | d2, a});
    if (!fresh) return at->second;
    std::vector<std::uint32_t>& out = at->second;
    for (const std::uint32_t x : back(a, d1))
      for (const std::uint32_t st : reach(p, x, d2))
        for (const LrItem& k : _gr.kernel(st))
          if (k.prod == p && k.dot == d2) {
            out.push_back(st);
            break;
          }
    lr_sort_unique(out);
    return out;
  }

  static std::uint32_t first_of(std::uint64_t e) {
    return static_cast<std::uint32_t>(e >> 32);
  }
  static std::uint32_t second_of(std::uint64_t e) {
    return static_cast<std::uint32_t>(e);
  }

  // Calls `f(x, y)` on the pairs of `xs` and `ys`, ascending by `key`, that
  // share one, the groups in turn and the first side's the outer. True where
  // `f` stops it.
  template <class T, class Key, class F>
  static bool join_keys(std::span<const T> xs, std::span<const T> ys,
                        Key&& key, F&& f) {
    std::size_t i = 0, j = 0;
    while (i < xs.size() && j < ys.size()) {
      const auto a = key(xs[i]), b = key(ys[j]);
      if (a < b) {
        ++i;
        continue;
      }
      if (b < a) {
        ++j;
        continue;
      }
      std::size_t ie = i + 1, je = j + 1;
      while (ie < xs.size() && key(xs[ie]) == a) ++ie;
      while (je < ys.size() && key(ys[je]) == a) ++je;
      for (std::size_t k = i; k < ie; ++k)
        for (std::size_t l = j; l < je; ++l)
          if (f(xs[k], ys[l])) return true;
      i = ie;
      j = je;
    }
    return false;
  }

  // Calls `f(group, hi, b, swap)` on each group of equal `key` in the
  // shallower of `xs` and `ys` (the other is `hi`, `swap` where it is `xs`) and
  // each state `b` a walk of production `p`'s symbols joins it to. True where
  // `f` stops it.
  template <class T, class Key, class F>
  bool each_linked(unsigned p, unsigned dx, std::span<const T> xs, unsigned dy,
                   std::span<const T> ys, Key&& key, F&& f) {
    const bool swap = dx > dy;
    const auto lo = swap ? ys : xs;
    const auto hi = swap ? xs : ys;
    for (std::size_t i = 0, ie; i < lo.size(); i = ie) {
      const std::uint32_t a = key(lo[i]);
      ie = i;
      while (ie < lo.size() && key(lo[ie]) == a) ++ie;
      for (const std::uint32_t b :
           link(p, std::min(dx, dy), std::max(dx, dy), a))
        if (f(lo.subspan(i, ie - i), hi, b, swap)) return true;
    }
    return false;
  }

  // Calls `f(x, y)` on the states of `xs` and `ys` the tops of two chains may
  // stand in on one stack: those alike where both chains go on under a top of
  // one dot, else those a walk of the production's symbols joins. True where
  // `f` stops it.
  template <class F>
  bool each_state(unsigned p, unsigned dx, bool xTall,
                  std::span<const std::uint64_t> xs, unsigned dy, bool yTall,
                  std::span<const std::uint64_t> ys, F&& f) {
    if (dx == dy && xTall && yTall) return join_keys(xs, ys, first_of, f);
    return each_linked(
        p, dx, xs, dy, ys, first_of,
        [&](std::span<const std::uint64_t> lo,
            std::span<const std::uint64_t> hi, std::uint32_t b, bool swap) {
          auto at = std::ranges::lower_bound(hi, std::uint64_t{b} << 32);
          for (; at != hi.end() && first_of(*at) == b; ++at)
            for (const std::uint64_t x : lo)
              if (swap ? f(*at, x) : f(x, *at)) return true;
          return false;
        });
  }

  static bool apart(std::uint32_t offa, std::uint32_t orga, std::uint32_t offb,
                    std::uint32_t orgb) {
    return offa == offb && orga != orgb;
  }

  // What two sequences of words say: their first difference, or where the
  // shorter one ends.
  Say sequence_say(std::uint32_t i, std::uint32_t j) {
    auto [say, fresh] = _sayOf.emplace(std::uint64_t{i} << 32 | j);
    if (!fresh) return say;
    const std::vector<Elem>& fx = _seqs[i];
    const std::vector<Elem>& fy = _seqs[j];
    std::size_t k = 0;
    while (k < fx.size() && k < fy.size() && fx[k] == fy[k]) ++k;
    if (k < fx.size() && k < fy.size())
      say.kind = fx[k].frame || fy[k].frame ? 0 : fx[k].v < fy[k].v ? -1 : 1;
    else
      say = {2, i, j};
    return say;
  }

  // What the chains of skeletons `sx` and `sy` say where their tops are one
  // node, `o` holding that node's origin, then the origins of the nodes under
  // each top. Two nodes of one depth must share an origin and differ in
  // production.
  Say say_of(std::uint32_t sx, std::uint32_t sy,
             const std::array<std::uint32_t, 5>& o) {
    const Skeleton& x = _skeletons[sx];
    const Skeleton& y = _skeletons[sy];
    const unsigned lx = x.len, ly = y.len;
    const std::uint32_t dx0 = x.at[0].dot, dy0 = y.at[0].dot;
    const std::uint32_t xo1 = dx0, xo2 = dx0 + x.at[1].dot;
    const std::uint32_t yo1 = dy0, yo2 = dy0 + y.at[1].dot;
    const std::uint32_t X = o[0];
    const std::uint32_t x1 = o[1], x2 = o[2], y1 = o[3], y2 = o[4];
    const bool hx1 = lx > 1, hx2 = lx > 2, hy1 = ly > 1, hy2 = ly > 2;
    const auto alike = [](unsigned pa, std::uint32_t oa, std::uint32_t ofa,
                          unsigned pb, std::uint32_t ob, std::uint32_t ofb) {
      return pa == pb && oa == ob && ofa == ofb;
    };
    if (hx1 && (apart(xo1, x1, 0, X) ||
                (hy1 && alike(x.at[1].prod, x1, xo1, y.at[1].prod, y1, yo1))))
      return {};
    if (hy1 && apart(0, X, yo1, y1)) return {};
    if (hx2 && (apart(xo2, x2, 0, X) ||
                (hy1 && (alike(x.at[2].prod, x2, xo2, y.at[1].prod, y1, yo1) ||
                         apart(xo2, x2, yo1, y1))) ||
                (hy2 && alike(x.at[2].prod, x2, xo2, y.at[2].prod, y2, yo2))))
      return {};
    if (hy2 && (apart(0, X, yo2, y2) ||
                (hx1 && (alike(x.at[1].prod, x1, xo1, y.at[2].prod, y2, yo2) ||
                         apart(xo1, x1, yo2, y2)))))
      return {};
    return sequence_say(seq_of(sx), seq_of(sy));
  }

  // What the chains `ca` over root `i` of `a` and `cb` over root `j` of `b`
  // say where their tops' dots stand in the states `ex` and `ey`. True where
  // the height is split both ways.
  bool incident(Shape& a, const Layout& la, const Chain& ca, unsigned i,
                Shape& b, const Layout& lb, const Chain& cb, unsigned j,
                std::uint64_t ex, std::uint64_t ey, Meeting& at) {
    const unsigned dx = ca.top.dot, dy = cb.top.dot;
    const std::uint32_t top = dx == 0   ? first_of(ex)
                              : dy == 0 ? first_of(ey)
                                        : kNone;
    return judge(skeleton_id(a, la, ca, i), skeleton_id(b, lb, cb, j),
                 {top, first_of(ex), second_of(ex), first_of(ey), second_of(ey)},
                 i, j, at);
  }

  // The same for a pair `meets` noted, over every pair of roots its chains
  // stand over: the way the states stood is all of them the say reads.
  bool incident(Shape& a, const Layout& la, Shape& b, const Layout& lb,
                const Meet& m, Meeting& at) {
    const Chain& ca = la.chains[m.x];
    const Chain& cb = lb.chains[m.y];
    const bool alike = ca.top.dot == cb.top.dot;
    for (const std::uint16_t i : a.members.of(ca.root)) {
      const std::uint32_t sx = skeleton_id(a, la, ca, i);
      for (const std::uint16_t j : b.members.of(cb.root)) {
        if (alike && next_of(a, ca, i) == next_of(b, cb, j)) continue;
        const std::uint32_t sy = skeleton_id(b, lb, cb, j);
        for (std::uint64_t ways = m.ways; ways != 0; ways &= ways - 1) {
          const auto& like = kLrAlikes.of[std::countr_zero(ways)];
          if (judge(sx, sy, {like[0], like[1], like[2], like[3], like[4]}, i,
                    j, at))
            return true;
        }
      }
    }
    return false;
  }

  // The say of the skeletons `sx` and `sy` with the origins `o` of what they
  // stand on; a tie is left to the tails.
  bool judge(std::uint32_t sx, std::uint32_t sy,
             const std::array<std::uint32_t, 5>& o, std::uint16_t xRoot,
             std::uint16_t yRoot, Meeting& at) {
    const Say say = say_of(sx, sy, o);
    if (say.kind == 2) {
      const Tie tie{xRoot, yRoot, say.x, say.y};
      if (_tieSeen
              .insert({say.x, say.y, std::uint32_t{tie.xRoot} << 16 | tie.yRoot})
              .second)
        at.ties.push_back(tie);
      return false;
    }
    if (say.kind != 0) (say.kind < 0 ? at.lower : at.upper) = true;
    return at.lower && at.upper;
  }

  // Which of the five origins of a pair of chains of two or three stand alike,
  // which is all `say_of` reads of them; the second of a chain of two stands
  // for nothing.
  std::uint32_t pattern(unsigned lx, unsigned ly,
                        const std::array<std::uint32_t, 5>& o) const {
    const std::uint32_t x2 = lx == 3 ? o[2] : kNone - 2;
    const std::uint32_t y2 = ly == 3 ? o[4] : kNone - 4;
    const unsigned l1 = o[1] == o[0] ? 0 : 1;
    const unsigned l2 = x2 == o[0] ? 0 : x2 == o[1] ? 1 : 2;
    const unsigned l3 = o[3] == o[0] ? 0 : o[3] == o[1] ? 1 : o[3] == x2 ? 2 : 3;
    const unsigned l4 = y2 == o[0]   ? 0
                        : y2 == o[1] ? 1
                        : y2 == x2   ? 2
                        : y2 == o[3] ? 3
                                     : 4;
    return kLrAlikes.id[l1 << 3 | l2 << 6 | l3 << 9 | l4 << 12];
  }

  // Notes that the chains `ca` of `A` and `cb` of `B` meet where their tops'
  // dots stand in the states `ex` and `ey`.
  bool note(const Layout& A, const Chain& ca, const Layout& B, const Chain& cb,
            std::uint64_t ex, std::uint64_t ey) {
    const unsigned dx = ca.top.dot, dy = cb.top.dot;
    const std::uint32_t top = dx == 0   ? first_of(ex)
                              : dy == 0 ? first_of(ey)
                                        : kNone;
    const std::array<std::uint32_t, 5> o{top, first_of(ex), second_of(ex),
                                         first_of(ey), second_of(ey)};
    const auto x = static_cast<std::uint32_t>(&ca - A.chains.data());
    const auto y = static_cast<std::uint32_t>(&cb - B.chains.data());
    const std::uint64_t way = std::uint64_t{1} << pattern(ca.len, cb.len, o);
    std::vector<Meet>& out = *_sink;
    auto [at, fresh] = _meetAt.emplace(std::uint64_t{x} << 32 | y);
    if (fresh) {
      at = static_cast<std::uint32_t>(out.size());
      out.push_back({x, y, way});
    } else {
      if ((out[at].ways & way) != 0) return false;
      out[at].ways |= way;
    }
    return incident(*_live->a, *_live->la, *_live->b, *_live->lb, {x, y, way},
                    *_live->at);
  }

  // `note` over the states of two chains of two or three.
  bool pair_up(const Layout& A, const Chain& ca, const Layout& B,
               const Chain& cb) {
    const unsigned dx = ca.top.dot, dy = cb.top.dot;
    if (dx == dy && ca.len == 3 && cb.len == 3 && ca.next == cb.next)
      return false;
    return each_state(ca.top.prod, dx, true,
               std::span(A.states).subspan(ca.from, ca.to - ca.from), dy, true,
               std::span(B.states).subspan(cb.from, cb.to - cb.from),
               [&](std::uint64_t ex, std::uint64_t ey) {
                 return note(A, ca, B, cb, ex, ey);
               });
  }

  static unsigned prod_of(const Run& r) {
    return static_cast<unsigned>(r.key >> 16 & 0xffff);
  }
  static unsigned dot_of(const Run& r) {
    return static_cast<unsigned>(r.key & 0xffff);
  }

  // `each_state` over the chains of three whose tops are of production `p` and
  // dots `dx`, `dy`. `gated`: their roots stand at one depth and so begin in one
  // state.
  bool join_dots(const Layout& A, const Layout& B, unsigned p, unsigned dx,
                 std::span<const Ent> xs, unsigned dy, std::span<const Ent> ys,
                 bool gated) {
    if (dx == dy)
      return join_keys(
          xs, ys, [&](const Ent& e) { return gated ? e.ab : e.ab >> 32; },
          [&](const Ent& x, const Ent& y) {
            const Chain& cx = A.chains[x.chain];
            const Chain& cy = B.chains[y.chain];
            return cx.next != cy.next && note(A, cx, B, cy, x.ab, y.ab);
          });
    return each_linked(
        p, dx, xs, dy, ys, [](const Ent& e) { return first_of(e.ab); },
        [&](std::span<const Ent> lo, std::span<const Ent> hi, std::uint32_t b,
            bool swap) {
          for (const Ent& l : lo) {
            const std::uint64_t want =
                std::uint64_t{b} << 32 | (gated ? second_of(l.ab) : 0);
            auto at = std::ranges::lower_bound(
                hi, want, {}, [](const Ent& e) { return e.ab; });
            for (; at != hi.end() &&
                   (gated ? at->ab == want : first_of(at->ab) == b);
                 ++at) {
              const Ent &ex = swap ? *at : l, &ey = swap ? l : *at;
              if (note(A, A.chains[ex.chain], B, B.chains[ey.chain], ex.ab,
                       ey.ab))
                return true;
            }
          }
          return false;
        });
  }

  // `join_dots` over every production and dot of two root offsets.
  bool join_class(const Layout& A, const Layout& B, std::span<const Ent> xe,
                  std::span<const Run> xs, std::span<const Ent> ye,
                  std::span<const Run> ys, bool gated) {
    const auto states = [](std::span<const Ent> e, const Run& r) {
      return e.subspan(r.begin, r.end - r.begin);
    };
    return join_keys(xs, ys, prod_of, [&](const Run& x, const Run& y) {
      return join_dots(A, B, prod_of(x), dot_of(x), states(xe, x), dot_of(y),
                       states(ye, y), gated);
    });
  }

  // How the tails of the readings stand laid past the bodies of `m`.
  int tailed(const Reading& ra, const Reading& rb, const Tie& m) {
    const std::vector<std::uint32_t>& tl = _keys[_tails[ra.tailAt + m.xRoot]];
    const std::vector<std::uint32_t>& tr = _keys[_tails[rb.tailAt + m.yRoot]];
    const std::vector<Elem>& left = _seqs[m.x];
    const std::vector<Elem>& right = _seqs[m.y];
    const std::size_t ln = left.size(), rn = right.size();
    if (ln == rn) {
      for (std::size_t i = 0; i < tl.size() && i < tr.size(); ++i)
        if (tl[i] != tr[i]) return tl[i] < tr[i] ? -1 : 1;
      return 0;
    }
    for (std::size_t i = std::min(ln, rn);; ++i) {
      if (i >= ln + tl.size() || i >= rn + tr.size()) return 0;
      const Elem a = i < ln ? left[i] : Elem{tl[i - ln], false};
      const Elem b = i < rn ? right[i] : Elem{tr[i - rn], false};
      if (a == b) continue;
      if (a.frame || b.frame) return 0;
      return a.v < b.v ? -1 : 1;
    }
  }

  using Ids = std::vector<std::uint64_t>;

  // The chains of `ids` whose tops are of production `p`.
  std::pair<Ids::const_iterator, Ids::const_iterator> with_prod(
      const Ids& ids, unsigned p) const {
    return {std::lower_bound(ids.begin(), ids.end(),
                             std::uint64_t{_dom.base[p]} << 32),
            std::lower_bound(ids.begin(), ids.end(),
                             std::uint64_t{_dom.base[p + 1]} << 32)};
  }

  // The places among the chains of two of `lay` of those whose height is `h`.
  std::pair<std::uint32_t, std::uint32_t> mids_at(const Layout& lay,
                                                  std::uint32_t h) const {
    const auto height = [&](std::uint32_t m) { return lay.chains[m].height; };
    const auto lo = std::ranges::lower_bound(lay.mids, h, {}, height);
    const auto hi = std::ranges::upper_bound(lay.mids, h, {}, height);
    return {static_cast<std::uint32_t>(lo - lay.mids.begin()),
            static_cast<std::uint32_t>(hi - lay.mids.begin())};
  }

  // What stands over a node at `height`:
  // each a top of a chain of `lay`, as item and state, ascending.
  const std::vector<std::uint64_t>& shorts_at(Layout& lay,
                                              std::uint32_t height) {
    Stand& st = lay.stand[height];
    if (st.shortMade) return st.shorts;
    st.shortMade = true;
    for (const std::uint64_t packed : lay.shortAt[height]) {
      const Chain& c = lay.chains[static_cast<std::uint32_t>(packed)];
      for (std::uint32_t k = c.from; k < c.to; ++k)
        st.shorts.push_back(packed >> 32 << 32 | first_of(lay.states[k]));
    }
    lr_sort_unique(st.shorts);
    return st.shorts;
  }

  // The same for the chains of three, as laying them would show.
  const std::vector<std::uint64_t>& talls_at(Layout& lay,
                                             std::uint32_t height) {
    Stand& st = lay.stand[height];
    if (st.tallMade) return st.talls;
    st.tallMade = true;
    for (unsigned d = 0; d <= _maxDot && d <= height; ++d) {
      const auto [lo, hi] = mids_at(lay, height - d);
      for (std::uint32_t mi = lo; mi < hi; ++mi) {
        Mid& md = _midPool[lay.chains[lay.mids[mi]].over];
        if (md.parentOff.empty()) lay_parents(md);
        for (std::uint32_t q = md.parentOff[d]; q < md.parentOff[d + 1]; ++q)
          st.talls.push_back(
              md.parents[q] >> 32 << 32 |
              first_of(md.over[static_cast<std::uint32_t>(md.parents[q])]));
      }
    }
    lr_sort_unique(st.talls);
    return st.talls;
  }

  // Whether `tops` holds a top of production `p` and dot `d` standing in `a`.
  bool stands_in(const std::vector<std::uint64_t>& tops, unsigned p,
                 unsigned d, std::uint32_t a) const {
    return std::ranges::binary_search(
        tops, std::uint64_t{_dom.base[p] + d} << 32 | a);
  }

  // Whether a root of `b` over `lay` is one; `s` is the state of the readings.
  bool stands_root(const Shape& b, const Layout& lay, std::uint32_t height,
                   unsigned p, unsigned d, std::uint32_t a, unsigned s) const {
    for (std::size_t i = 0; i < b.roots.size(); ++i)
      if (lay.rootChain[b.cls[i]] != kNone && b.roots[i].dot == height &&
          b.roots[i].dot == d && b.roots[i].prod == p && s == a)
        return true;
    return false;
  }

  // The states `d` symbols of production `p` on from state `x`, ascending; the
  // walk is kept for the next ask of the same node.
  const std::vector<std::uint32_t>& reach(unsigned p, std::uint32_t x,
                                          unsigned d) {
    if (_reachP != p || _reachX != x) {
      _reachP = p;
      _reachX = x;
      _reachMade = 0;
      _reach[0].assign(1, x);
    }
    const LrLowProd& pr = _g.prods[p];
    for (; _reachMade < d; ++_reachMade) {
      std::vector<std::uint32_t>& to = _reach[_reachMade + 1];
      to.clear();
      for (const std::uint32_t st : _reach[_reachMade])
        for (const LrEdge& e : _gr.edges(st, _g.sym_at(pr, _reachMade)))
          to.push_back(e.to);
      lr_sort_unique(to);
    }
    return _reach[d];
  }

  // Whether the node of production `p` that began in `x` tops one of `tops`.
  bool holds(const std::vector<std::uint64_t>& tops, unsigned p,
             std::uint32_t x) {
    for (unsigned d = 0; d <= _g.prods[p].len && d <= _maxDot; ++d) {
      const std::vector<std::uint32_t>& here = reach(p, x, d);
      if (here.empty()) break;
      for (const std::uint32_t st : here)
        if (std::ranges::binary_search(
                tops, std::uint64_t{_dom.base[p] + d} << 32 | st))
          return true;
    }
    return false;
  }

  // Whether it tops a root of `b` over `lay`, whose state is `s`.
  bool holds_root(const Shape& b, const Layout& lay, std::uint32_t height,
                  unsigned p, std::uint32_t x, unsigned s) {
    for (std::size_t i = 0; i < b.roots.size(); ++i) {
      if (lay.rootChain[b.cls[i]] == kNone || b.roots[i].dot != height ||
          b.roots[i].prod != p)
        continue;
      if (!std::ranges::binary_search(reach(p, x, height), s)) continue;
      for (const LrItem& kk : _gr.kernel(s))
        if (kk.prod == p && kk.dot == height) return true;
    }
    return false;
  }

  // The states in which the chains of two of `s` with this top begin.
  const std::vector<std::uint32_t>& begins_of(Layout& s, std::uint32_t height,
                                              const LrItem& item) {
    const std::uint64_t key = std::uint64_t{height} << 32 | item_ix(item);
    const auto [at, fresh] = s.begins.try_emplace(key);
    if (!fresh) return at->second;
    const auto [lo, hi] = mids_at(s, height);
    const unsigned ix = item_ix(item);
    const auto top = [&](std::uint32_t m) { return item_ix(s.chains[m].top); };
    for (auto it = std::ranges::lower_bound(s.mids.begin() + lo,
                                            s.mids.begin() + hi, ix, {}, top);
         it != s.mids.begin() + hi && top(*it) == ix; ++it) {
      for (const std::uint64_t e : _midPool[s.chains[*it].over].over)
        at->second.push_back(first_of(e));
    }
    lr_sort_unique(at->second);
    return at->second;
  }

  // Whether `a` is in the ascending `v` from `at`, which it leaves at the place
  // of `a`.
  static bool seek(const std::vector<std::uint32_t>& v, std::size_t& at,
                   std::uint32_t a) {
    at = static_cast<std::size_t>(
        std::lower_bound(v.begin() + at, v.end(), a) - v.begin());
    return at < v.size() && v[at] == a;
  }

  // Whether no chain of shape `ci` goes on past its top level to meet shape
  // `oi`: each node there is held by `oi`, repeats one below it, or has nothing
  // over it. Where the layouts alone hold it so, the roots cannot undo that.
  bool closed(unsigned s, std::uint32_t ci, std::uint32_t oi) {
    const Shape& cs = _shapes[ci];
    const Shape& os = _shapes[oi];
    if (ci == oi || _layouts[cs.layout].chains.empty()) return true;
    auto [bare, fresh] =
        _closedOf.emplace(std::uint64_t{cs.layout} << 32 | os.layout);
    if (fresh) bare = closes(s, cs, os, false) ? 1 : 2;
    return bare == 1 || closes(s, cs, os, true);
  }

  // The same, with the roots of the shapes in the count or not.
  bool closes(unsigned s, const Shape& cs, const Shape& os, bool roots) {
    Layout& c = _layouts[cs.layout];
    Layout& other = _layouts[os.layout];
    const std::vector<std::uint64_t> none;
    for (const std::uint32_t m : c.mids) {
      const Chain mc = c.chains[m];
      const std::vector<std::uint64_t>& over = _midPool[mc.over].over;
      // Another reading with this node of two has every node over it.
      const std::vector<std::uint32_t>& theirs =
          begins_of(other, mc.height, mc.top);
      std::size_t seen = 0;
      for (std::size_t i = 0; i < over.size(); ++i) {
        const std::uint32_t a = first_of(over[i]);
        if (i != 0 && first_of(over[i - 1]) == a) continue;
        if (seek(theirs, seen, a)) continue;
        bool whole = true;
        each_parent(a, mc.top.prod, [&](const LrItem& k) {
          if (!whole) return;
          const std::uint32_t height = mc.height + k.dot;
          const std::vector<std::uint64_t>& mine = shorts_at(c, height);
          if (stands_in(mine, k.prod, k.dot, a) ||
              (roots &&
               stands_root(cs, c, height, k.prod, k.dot, a, s)))
            return;
          const bool reached = height < other.stand.size();
          const std::vector<std::uint64_t>& theirs =
              reached ? shorts_at(other, height) : none;
          const std::vector<std::uint64_t>& taller =
              reached ? talls_at(other, height) : none;
          if (stands_in(theirs, k.prod, k.dot, a) ||
              stands_in(taller, k.prod, k.dot, a) ||
              (roots &&
               stands_root(os, other, height, k.prod, k.dot, a, s)))
            return;
          for (const std::uint32_t x : back(a, k.dot)) {
            if (!has_parent(x, k.prod)) continue;
            if (holds(mine, k.prod, x) || holds(theirs, k.prod, x) ||
                holds(taller, k.prod, x) ||
                (roots && (holds_root(cs, c, height, k.prod, x, s) ||
                           holds_root(os, other, height, k.prod, x, s))))
              continue;
            whole = false;
            return;
          }
        });
        if (!whole) return false;
      }
    }
    return true;
  }

  Decision decide(unsigned s, const Reading& ra, const Reading& rb) {
    const std::uint32_t sa = shape_of(s, ra.list), sb = shape_of(s, rb.list);
    const Meetings& all = meetings(s, sa, sb);
    int verdict = 0;
    for (const Meeting& m : all.list) {
      bool first = m.lower, second = m.upper;
      for (const Tie& t : m.ties) {
        const int v = tailed(ra, rb, t);
        if (v != 0) (v < 0 ? first : second) = true;
      }
      if (first != second) {
        verdict = first ? -1 : 1;
        break;
      }
    }
    if (verdict == 0) return {};
    return {verdict, all.complete && !all.tied && all.lower != all.upper};
  }

  const LrBuildCfg& _g;
  const LrItemDomain& _dom;
  const LrGraph& _gr;
  const LrClosures& _cl;
  const LrFirstSets& _fs;
  const LrBuckets<std::uint16_t>& _pn;
  const LrBuildTable& _A;
  std::vector<std::vector<std::uint32_t>> _preds;
  std::vector<std::uint32_t> _seen;
  std::uint32_t _seenGen = 0;
  std::vector<DynSet> _follow;
  std::vector<std::vector<std::uint32_t>> _keys;
  std::vector<std::vector<std::uint32_t>> _corner;
  std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> _back;
  std::unordered_map<std::uint64_t, std::vector<std::int8_t>> _scoreRows;
  std::vector<std::uint32_t> _climbOf;
  std::unordered_map<std::vector<std::uint32_t>, std::uint32_t, VecHash>
      _climbs;
  std::vector<Row> _rows;
  std::vector<Opens> _opens;
  std::vector<std::uint32_t> _tails;
  std::vector<std::uint32_t> _first, _fill, _csrTail;
  std::vector<LrItem> _csrItem;
  FlatMap<Reading> _generic;
  std::vector<Shape> _shapes;
  std::vector<Layout> _layouts;
  std::vector<Mid> _midPool;
  std::unordered_multimap<std::uint64_t, std::uint32_t> _midIds;
  std::vector<Rise> _rises;
  std::vector<const std::vector<std::uint32_t>*> _origins;
  std::unordered_map<std::vector<std::uint32_t>, std::uint32_t, VecHash>
      _shapeOf, _layoutIds, _shapeIds;
  FlatMap<std::uint32_t> _originOf;
  std::unordered_multimap<std::uint64_t, std::uint32_t> _originIds, _riseIds;
  std::unordered_map<std::array<std::uint32_t, 3>, std::uint32_t, SegHash>
      _riseOf;
  std::unordered_map<std::uint64_t, Pair> _pairs;
  std::vector<Meet>* _sink = nullptr;
  Live* _live = nullptr;
  FlatMap<std::uint32_t> _meetAt;
  std::size_t _held = 0;
  FlatMap<std::uint32_t> _meetOf;
  std::vector<Meetings> _meetings;
  std::vector<std::vector<Elem>> _seqs;
  std::unordered_map<std::vector<std::uint64_t>, std::uint32_t, VecHash> _seqOf;
  std::vector<std::uint64_t> _seqKey;
  std::unordered_set<std::array<std::uint32_t, 3>, SegHash> _tieSeen;
  std::vector<char> _restEmpty;
  std::vector<DynSet> _after;
  std::vector<char> _afterMade;
  std::uint32_t _maxDot = 0;
  std::vector<std::vector<Next>> _nexts;
  std::vector<char> _nextsMade;
  std::vector<std::uint32_t> _items, _offsets, _byKey, _laid;
  std::vector<std::vector<std::uint64_t>> _byDot;
  std::vector<std::uint64_t> _byItem;
  std::vector<std::uint32_t> _bucketOf;
  std::vector<std::uint64_t> _laying;
  std::vector<std::vector<std::uint32_t>> _reach;
  std::uint32_t _reachP = kNone, _reachX = kNone, _reachMade = 0;
  std::vector<std::vector<LrItem>> _parentItems;
  std::vector<char> _parentItemsBuilt;
  std::vector<Elem> _fx;
  FlatMap<Say> _sayOf;
  FlatMap<std::uint32_t> _skeletonOf;
  FlatMap<std::uint8_t> _closedOf;
  std::vector<Skeleton> _skeletons;
  std::unordered_map<std::array<std::uint32_t, 3>, std::vector<std::uint32_t>,
                     SegHash>
      _links;
};

// A shift/reduce both sides of which sit on a precedence step: the tighter
// step wins, and at equal steps the associativity -- which a non-associative
// step answers with neither.
void lr_drop_by_precedence(const LrBuildCfg& g, const LrAction* merged,
                           char* drop, std::size_t i, std::size_t j) {
  std::size_t sh = j;
  for (std::size_t k = i; k < j; ++k)
    if (lr_takes_token(merged[k].kind)) sh = k;
  if (sh == j) return;
  const TermBuild& tk = g.termBuild[merged[i].term];
  if (tk.prec != 0)
    for (std::size_t k = i; k < j; ++k) {
      if (k == sh || merged[k].kind != LrActionKind::reduce) continue;
      const std::uint8_t pp = g.prodBuild[merged[k].arg].prec;
      if (pp == 0) continue;
      if (pp == tk.prec && tk.assoc == Association::non)
        drop[sh] = drop[k] = 1;
      else if (pp > tk.prec || (pp == tk.prec && tk.assoc == Association::left))
        drop[sh] = 1;
      else
        drop[k] = 1;
    }
}

// The `n` readings in the order `verdict` gives, in `kept`: a selection and
// not a sort, as the order owes no transitivity. The first left that none
// left precedes goes next.
struct LrSelection {
  std::vector<std::int8_t> verdicts;
  std::vector<std::size_t> kept;

  void operator()(std::size_t n, auto&& verdict) {
    verdicts.assign(n * n, 0);
    _preceded.assign(n, 0);
    _precedes.assign(n, 0);
    for (std::size_t a = 0; a < n; ++a)
      for (std::size_t b = a + 1; b < n; ++b) {
        const int v = verdict(a, b);
        if (v == 0) continue;
        verdicts[a * n + b] = static_cast<std::int8_t>(v);
        verdicts[b * n + a] = static_cast<std::int8_t>(-v);
        ++_preceded[v < 0 ? b : a];
        ++_precedes[v < 0 ? a : b];
      }
    _taken.assign(n, 0);
    kept.clear();
    for (std::size_t head = 0; kept.size() < n;) {
      while (_taken[head]) ++head;
      std::size_t pick = head;
      for (std::size_t c = head; c < n; ++c)
        if (!_taken[c] && _preceded[c] == 0) {
          pick = c;
          break;
        }
      _taken[pick] = 1;
      kept.push_back(pick);
      if (_precedes[pick] == 0) continue;
      for (std::size_t c = head; c < n; ++c)
        if (!_taken[c] && verdicts[pick * n + c] < 0) --_preceded[c];
    }
  }

 private:
  std::vector<std::size_t> _preceded, _precedes;
  std::vector<char> _taken;
};

// Every fused entry's row: its members' actions deduplicated, decided or put
// in the order the drive forks them; the owed ambiguities counted and the goto
// lanes laid. False where a cap gave way.
bool lr_merge_entries(LrBuildTable& A, const LrGraph& gr,
                      const LrActions& actions,
                      const std::vector<unsigned>& newId,
                      const LrLookback& lb, LrWrittenOrder& order) {
  const auto& g = A.cfg;
  const ChainPool& writes = A.writes;
  const unsigned nT = g.nterms;
  const unsigned ns = gr.states();
  // One key per ambiguity, deduplicated at the end.
  std::vector<std::uint64_t> owedKeys;
  constexpr std::uint32_t kNoMerged = 0xffffffffu;
  std::vector<std::uint32_t> headTerm(nT);
  std::vector<std::uint32_t> headGen(nT, std::uint32_t{});
  std::uint32_t entryGen = 0;
  const LrBuckets<> mem(A.nstates, ns,
                        [&](std::uint32_t s) { return newId[s]; });
  LrGotoRows lanes;
  // A guard's program landing on fused entries, interned.
  std::unordered_map<std::vector<std::uint32_t>, std::uint32_t, VecHash>
      guardIx;
  std::vector<std::uint32_t> guardKey;
  const auto intern_guard = [&](const LrGuard& p) -> std::uint32_t {
    lr_guard_key(p, [&](std::uint16_t t) { return newId[t]; }, guardKey);
    const auto [at, fresh] = guardIx.try_emplace(
        guardKey, static_cast<std::uint32_t>(A.guards.size()));
    if (!fresh) return at->second;
    LrGuard& q = A.guards.emplace_back(p);
    for (std::uint16_t& t : q.to)
      if (t != LrGuard::kNoState) t = static_cast<std::uint16_t>(newId[t]);
    return at->second;
  };

  // What each member of a fused state holds on a terminal: a token taken, or
  // a reduction or acceptance by its production.
  const auto held = [](const LrAction& a) {
    const bool takes = lr_takes_token(a.kind);
    return std::uint64_t{a.term} << 32 |
           std::uint64_t{takes ? 0u : 1u + unsigned(a.kind)} << 16 |
           (takes || a.kind == LrActionKind::accept ? 0u : a.arg);
  };
  std::vector<std::vector<std::uint64_t>> heldBy;
  // How two actions of fused state `f` stand in the order written: what every
  // member holding both says, or nothing where two disagree; and whether every
  // member says it for every text.
  const auto written = [&](unsigned f, const LrAction& x, const LrAction& y,
                           bool& exact) {
    const auto members = mem.of(f);
    if (members.size() == 1) {
      const auto [v, certain] = order(members[0], x, y, x.term);
      exact = certain && v != 0;
      return v;
    }
    int verdict = 0;
    exact = true;
    std::size_t m = 0;
    for (const std::uint32_t s : members) {
      const std::vector<std::uint64_t>& has = heldBy[m++];
      if (!std::ranges::binary_search(has, held(x)) ||
          !std::ranges::binary_search(has, held(y)))
        continue;
      const auto [v, certain] = order(s, x, y, x.term);
      exact = exact && certain;
      if (v != 0 && verdict != 0 && v != verdict) return 0;
      if (v != 0) verdict = v;
    }
    exact = exact && verdict != 0;
    return verdict;
  };
  std::vector<char> sure;
  LrSelection select;
  std::vector<LrAction> ordered;

  DynSet implied;
  if (A.identity == LrIdentity::core)
    for (unsigned t = 1; t < nT; ++t)
      if (g.terms[t].laid == kLaidImplied) implied.set(t);

  std::vector<LrAction> merged, mtmp;
  std::vector<char> drop;
  std::vector<std::uint32_t> nextMerged;
  std::vector<std::uint32_t> mcnt(nT + 1, std::uint32_t{});
  for (unsigned f = 0; f < A.nstates; ++f) {
    merged.clear();
    nextMerged.clear();
    ++entryGen;
    if (implied.more_than_one() &&
        std::ranges::any_of(mem.of(f), [&](std::uint32_t s) {
          return lb.implied_mixed(s, implied);
        }))
      ++A.nImpliedMixed;
    const std::size_t entryAt = lanes.edges.size();
    const auto members = mem.of(f);
    const bool fused = members.size() > 1;
    if (fused) heldBy.resize(std::max(heldBy.size(), members.size()));
    std::size_t member = 0;
    for (const std::uint32_t s : members) {
      auto* const mine = fused ? &heldBy[member++] : nullptr;
      if (fused) mine->clear();
      actions.each(s, [&](const LrAction& a) {
        if (fused) mine->push_back(held(a));
        LrAction out = a;
        if (a.kind == LrActionKind::shift)
          out.arg = static_cast<std::uint16_t>(newId[a.arg]);
        else if (a.kind == LrActionKind::guard)
          out.arg = static_cast<std::uint16_t>(intern_guard(gr.progs[a.arg]));
        bool dup = false;
        const std::uint32_t head =
            headGen[out.term] == entryGen ? headTerm[out.term] : kNoMerged;
        for (std::uint32_t mi = head; mi != kNoMerged; mi = nextMerged[mi]) {
          LrAction& m = merged[mi];
          if (m.kind == out.kind && m.arg == out.arg) {
            dup = true;
            // Two sites leave the caret without one.
            if (!lr_same_site(writes, m.site, out.site)) m.site = kNoElement;
            if (!lr_same_site(writes, m.outer, out.outer)) m.outer = kNoElement;
            break;
          }
        }
        if (!dup) {
          nextMerged.push_back(head);
          headGen[out.term] = entryGen;
          headTerm[out.term] = static_cast<std::uint32_t>(merged.size());
          merged.push_back(out);
        }
      });
      if (fused) std::ranges::sort(*mine);
      // Only the members before this one can hold one of its edges.
      const std::size_t memberAt = lanes.edges.size();
      const std::span<const LrEdge> row = gr.gotos[s];
      for (std::size_t k = 0; k < row.size(); ++k) {
        const LrEdge& x = row[k];
        if (x.prog != 0 && k != 0 && row[k - 1].sym == x.sym) continue;
        const LrGotoRows::Edge e{
            static_cast<std::uint16_t>(nt_index(x.sym)),
            static_cast<std::uint16_t>(newId[x.to]),
            x.prog == 0 ? 0u : intern_guard(gr.progs[x.prog - 1u]) + 1u};
        if (std::none_of(lanes.edges.begin() + entryAt,
                         lanes.edges.begin() + memberAt,
                         [&](const LrGotoRows::Edge& z) {
                           return z.nt == e.nt && z.to == e.to &&
                                  z.guard == e.guard;
                         }))
          lanes.edges.push_back(e);
      }
    }
    lanes.close_row();
    A.tagKnown[f] = gr.tagKnown[mem.of(f)[0]];
    A.tagTruth[f] = gr.tagTruth[mem.of(f)[0]];
    // The order the drive forks in: the shift, then accept, then the
    // reductions, where the order written says nothing.
    {
      const std::size_t mn0 = merged.size();
      if (mtmp.size() < mn0) mtmp.resize(mn0);
      DynSet mmask;
      for (std::size_t i = 0; i < mn0; ++i) {
        ++mcnt[merged[i].term];
        mmask.set(merged[i].term);
      }
      lr_sort_on_term(merged.data(), mn0, mmask, mcnt.data(), mtmp.data());
      const auto place = [](LrActionKind k) {
        return lr_takes_token(k) ? 0 : k == LrActionKind::accept ? 1 : 2;
      };
      lr_each_run(merged.data(), mn0, [&](std::size_t i, std::size_t j) {
        for (std::size_t k = i + 1; k < j; ++k) {
          const LrAction v = merged[k];
          std::size_t m = k;
          while (m > i && (place(merged[m - 1].kind) > place(v.kind) ||
                           (merged[m - 1].kind == v.kind &&
                            merged[m - 1].arg > v.arg))) {
            merged[m] = merged[m - 1];
            --m;
          }
          merged[m] = v;
        }
      });
    }
    const std::size_t mn = merged.size();
    drop.assign(mn, 0);
    lr_each_run(merged.data(), mn, [&](std::size_t i, std::size_t j) {
      lr_drop_by_precedence(g, merged.data(), drop.data(), i, j);
    });
    std::size_t live = 0;
    for (std::size_t k = 0; k < mn; ++k)
      if (!drop[k]) merged[live++] = merged[k];
    merged.resize(live);
    // The order written decides a rank, not a death: the later reading of a
    // reduce/reduce stands where the earlier one fails, which no construction
    // can know (C's `g(a, b);`). It orders the forks.
    order.trim();
    lr_each_run(
        merged.data(), merged.size(), [&](std::size_t i, std::size_t j) {
          const std::size_t n = j - i;
          if (n == 1) {
            merged[i].exact = false;
            return;
          }
          sure.assign(n * n, 0);
          select(n, [&](std::size_t a, std::size_t b) {
            bool exact = false;
            const int v = written(f, merged[i + a], merged[i + b], exact);
            sure[a * n + b] = exact;
            return v;
          });
          const std::vector<std::size_t>& kept = select.kept;
          // The order stands for every text where each pair is exact and the
          // selection kept it.
          bool exact = true;
          for (std::size_t a = 0; a < n && exact; ++a)
            for (std::size_t b = a + 1; b < n && exact; ++b)
              exact = sure[std::min(kept[a], kept[b]) * n +
                           std::max(kept[a], kept[b])] &&
                      select.verdicts[kept[a] * n + kept[b]] < 0;
          // Reductions nothing written tells apart are owed to the graph.
          for (std::size_t a = 0; a < n; ++a)
            for (std::size_t b = a + 1; b < n; ++b) {
              const LrAction& x = merged[i + kept[a]];
              const LrAction& y = merged[i + kept[b]];
              if (x.kind != LrActionKind::reduce ||
                  y.kind != LrActionKind::reduce ||
                  select.verdicts[kept[a] * n + kept[b]] != 0)
                continue;
              ++A.nOrdered;
              owedKeys.push_back((std::uint64_t{1} << 62) |
                                 (static_cast<std::uint64_t>(x.arg) << 16) |
                                 y.arg);
              if (std::ranges::any_of(mem.of(f), [&](std::uint32_t s) {
                    return lb.merge_induced(s, x.arg, y.arg, x.term);
                  })) {
                if (A.nMergeInduced == 0) {
                  A.mergeState = static_cast<std::uint16_t>(f);
                  A.mergeTerm = x.term;
                }
                ++A.nMergeInduced;
              }
            }
          ordered.clear();
          for (const std::size_t k : kept) {
            ordered.push_back(merged[i + k]);
            ordered.back().exact = exact;
          }
          std::ranges::copy(ordered,
                            merged.begin() + static_cast<std::ptrdiff_t>(i));
        });
    if (merged.size() >= 0xffffu) {
      A.cap_hit("one parser state has more than 65 534 actions");
      return false;
    }
    A.actionBase[f] = A.nactions;
    const std::size_t mrn = merged.size();
    LrAction* const wrow = A.actions.row(A.nactions, mrn);
    DynSet& frow = A.follow[f];
    for (std::size_t z = 0; z < mrn; ++z) {
      const LrAction& a = merged[z];
      if (z != 0 && a.term == merged[z - 1].term) {
        ++A.nconflicts;
        // The pair (this terminal, that reduction) is what stays undecided.
        const LrAction& red =
            a.kind == LrActionKind::reduce ? a : merged[z - 1];
        owedKeys.push_back((std::uint64_t{2} << 62) |
                           (static_cast<std::uint64_t>(a.term) << 16) |
                           red.arg);
      }
      wrow[z] = a;
      frow.set(a.term);
    }
    A.nactions += static_cast<std::uint32_t>(mrn);
    A.actionCount[f] = static_cast<std::uint16_t>(mrn);
  }
  lr_sort_unique(owedKeys);
  A.nOwed = static_cast<std::uint32_t>(owedKeys.size());

  A.set_gotos(std::move(lanes));
  return true;
}

// The order a repair tries the terminals of a state: the order a top-down,
// left-to-right reading of the grammar meets them from the items the state
// holds. The innermost item reads first, a loop that goes on before the rule
// that takes what it grew; a rule that can end hands over to the states it
// returns to.
class LrInsertOrder {
 public:
  LrInsertOrder(const LrBuildCfg& g, const LrItemDomain& dom,
                const std::vector<std::uint8_t>& nullable, const LrGraph& gr)
      : _g(g), _dom(dom), _nullable(nullable), _gr(gr),
        _arm(lr_by_lhs(g)), _rank(g.nprods, kUnread), _nullFrom(g.nprods, 0),
        _seen(g.nnts, 0), _met(g.nterms, 0), _visited(gr.states(), 0),
        _under(gr.states(), 0), _preds(gr.predecessors()),
        _succ(gr.states()), _succMade(gr.states(), 0),
        _listed(gr.states(), 0), _coreOf(gr.states()) {
    for (unsigned n = 0; n < g.nnts; ++n)
      std::stable_sort(_arm.item.begin() + _arm.base[n],
                       _arm.item.begin() + _arm.base[n + 1],
                       [&](std::uint16_t a, std::uint16_t b) {
                         const LrProdBuild& x = g.prodBuild[a];
                         const LrProdBuild& y = g.prodBuild[b];
                         return x.priorityAlt != y.priorityAlt
                                    ? x.priorityAlt < y.priorityAlt
                                    : x.prioritySubset > y.prioritySubset;
                       });
    rank_productions();
    for (unsigned p = 0; p < g.nprods; ++p) {
      const LrLowProd& q = g.prods[p];
      unsigned from = q.len;
      while (from > 0 && !is_term(g.sym_at(q, from - 1)) &&
             nullable[nt_index(g.sym_at(q, from - 1))])
        --from;
      _nullFrom[p] = static_cast<std::uint16_t>(from);
    }
    order_items();
    group_kernels();
  }

  // The terminals of `row` (those marked `stamp` there, `want` of them) that
  // the canonical state made of `members` meets, in the order it meets them;
  // the walk stops once it has met them all.
  void of(std::span<const std::uint32_t> members,
          std::span<const std::uint32_t> row, std::uint32_t stamp,
          std::size_t want, std::vector<std::uint16_t>& out) {
    out.clear();
    ++_gen;
    _level.assign(members.begin(), members.end());
    for (const std::uint32_t s : _level) _visited[s] = _gen;
    while (!_level.empty() && out.size() < want) {
      ++_layer;
      _picked.clear();
      for (const std::uint32_t s : _level) {
        const std::uint32_t c = _coreOf[s];
        if (_reached[c] == _layer) continue;
        _reached[c] = _layer;
        for (const std::uint32_t at : _cores[c])
          if (_placed[at] != _layer) {
            _placed[at] = _layer;
            _picked.push_back(at);
          }
      }
      std::ranges::sort(_picked);
      read(row, stamp, want, out);
      _next.clear();
      for (const std::uint32_t s : _level)
        for (const std::uint32_t t : returns(s))
          if (_visited[t] != _gen) {
            _visited[t] = _gen;
            _next.push_back(t);
          }
      _level.swap(_next);
    }
  }

 private:
  static constexpr std::uint64_t kUnread = ~std::uint64_t{0};
  static constexpr std::uint64_t kStays = std::uint64_t{1} << 32;

  // Each production's place in a reading of the grammar from its start, a
  // nonterminal opened where it is first met; a turn of a left recursion
  // ranks before the rules that read what it grew.
  void rank_productions() {
    const LrBuildCfg& g = _g;
    std::vector<std::uint8_t> opened(g.nnts, 0);
    std::vector<std::pair<std::uint16_t, std::uint16_t>> todo{
        {g.startProd, 0}};
    std::uint64_t count = 0;
    while (!todo.empty()) {
      auto [p, i] = todo.back();
      todo.pop_back();
      const LrLowProd& q = g.prods[p];
      if (i == 0) _rank[p] = count++;
      for (; i < q.len; ++i) {
        const std::uint16_t y = g.sym_at(q, i);
        if (is_term(y) || opened[nt_index(y)] != 0) continue;
        const unsigned b = nt_index(y);
        opened[b] = 1;
        todo.push_back({p, static_cast<std::uint16_t>(i + 1u)});
        for (const std::uint16_t arm : std::views::reverse(_arm.of(b)))
          todo.push_back({arm, 0});
        break;
      }
    }
    for (unsigned p = 0; p < g.nprods; ++p)
      if (_rank[p] == kUnread) _rank[p] = count++;
    for (unsigned p = 0; p < g.nprods; ++p)
      if (!g.turns(p)) _rank[p] |= kStays;
  }

  // Every item in the order it reads, and the place of each.
  void order_items() {
    const LrBuildCfg& g = _g;
    struct Keyed {
      std::uint64_t key;
      LrItem item;
    };
    std::vector<Keyed> all;
    for (unsigned p = 0; p < g.nprods; ++p)
      for (unsigned d = 0; d <= g.prods[p].len; ++d)
        all.push_back({std::uint64_t{d} << 33 | _rank[p],
                       {static_cast<std::uint16_t>(p),
                        static_cast<std::uint16_t>(d)}});
    std::ranges::sort(all, {}, &Keyed::key);
    _placeOf.resize(all.size());
    _itemAt.resize(all.size());
    _placed.assign(all.size(), 0);
    for (std::uint32_t at = 0; at < all.size(); ++at) {
      _itemAt[at] = all[at].item;
      _placeOf[_dom.base[all[at].item.prod] + all[at].item.dot] = at;
    }
  }

  // The states that hold the same items read the same: one entry each.
  void group_kernels() {
    std::unordered_map<std::vector<std::uint32_t>, std::uint32_t, VecHash> ids;
    std::vector<std::uint32_t> key;
    for (unsigned s = 0; s < _gr.states(); ++s) {
      key.clear();
      for (const LrItem& k : _gr.kernel(s))
        key.push_back(_placeOf[_dom.base[k.prod] + k.dot]);
      lr_sort_unique(key);
      const auto [at, fresh] =
          ids.try_emplace(key, static_cast<std::uint32_t>(_cores.size()));
      if (fresh) _cores.push_back(key);
      _coreOf[s] = at->second;
    }
    _reached.assign(_cores.size(), 0);
  }

  // Reads `_picked` in order: a terminal of `row` is met, a nonterminal opens
  // its alternatives depth first, once, and a nullable one lets the item go on.
  void read(std::span<const std::uint32_t> row, std::uint32_t stamp,
            std::size_t want, std::vector<std::uint16_t>& out) {
    _todo.clear();
    for (std::size_t i = _picked.size(); i-- > 0;)
      _todo.push_back({_itemAt[_picked[i]].prod, _itemAt[_picked[i]].dot});
    while (!_todo.empty() && out.size() < want) {
      auto [p, j] = _todo.back();
      _todo.pop_back();
      const LrLowProd& q = _g.prods[p];
      for (; j < q.len; ++j) {
        const std::uint16_t y = _g.sym_at(q, j);
        if (is_term(y)) {
          if (_met[y] != _gen) {
            _met[y] = _gen;
            if (row[y] == stamp) out.push_back(y);
          }
          break;
        }
        const unsigned b = nt_index(y);
        if (_seen[b] != _gen) {
          _seen[b] = _gen;
          if (_nullable[b])
            _todo.push_back({p, static_cast<std::uint16_t>(j + 1u)});
          for (const std::uint16_t arm : std::views::reverse(_arm.of(b)))
            _todo.push_back({arm, 0});
          break;
        }
        if (!_nullable[b]) break;
      }
    }
  }

  // The states the rules of the kernel items of `s` that can end return to:
  // the goto on the left side of each from the states under its symbols.
  const std::vector<std::uint32_t>& returns(unsigned s) {
    std::vector<std::uint32_t>& out = _succ[s];
    if (_succMade[s]) return out;
    _succMade[s] = 1;
    ++_stamp;
    const std::uint64_t mine = _stamp;
    _asked.clear();
    for (const LrItem& k : _gr.kernel(s)) {
      if (k.dot < _nullFrom[k.prod]) continue;
      const std::uint16_t lhs = _g.prods[k.prod].lhs;
      const std::uint32_t ask = std::uint32_t{lhs} << 16 | k.dot;
      if (std::ranges::find(_asked, ask) != _asked.end()) continue;
      _asked.push_back(ask);
      const auto here = lr_back(_preds, _under, _stamp, s, k.dot);
      for (const std::uint32_t b : here)
        for (const LrEdge& e : _gr.edges(b, nt_sym(lhs)))
          if (_listed[e.to] != mine) {
            _listed[e.to] = mine;
            out.push_back(e.to);
          }
    }
    return out;
  }

  const LrBuildCfg& _g;
  const LrItemDomain& _dom;
  const std::vector<std::uint8_t>& _nullable;
  const LrGraph& _gr;
  LrBuckets<std::uint16_t> _arm;
  std::vector<std::uint64_t> _rank;
  std::vector<std::uint16_t> _nullFrom;
  std::uint32_t _gen = 0;
  std::vector<std::uint32_t> _seen, _met, _visited;
  std::uint64_t _stamp = 0;
  std::vector<std::uint64_t> _under;
  std::vector<std::vector<std::uint32_t>> _preds;
  std::vector<std::vector<std::uint32_t>> _succ;
  std::vector<char> _succMade;
  std::vector<std::uint64_t> _listed;
  std::vector<std::uint32_t> _asked;
  std::vector<std::uint32_t> _placeOf, _placed, _reached, _coreOf, _picked;
  std::vector<LrItem> _itemAt;
  std::vector<std::vector<std::uint32_t>> _cores;
  std::uint32_t _layer = 0;
  std::vector<std::uint32_t> _level, _next;
  std::vector<std::pair<std::uint16_t, std::uint16_t>> _todo;
};

// The terminals a repair may insert in each state, in the order the grammar
// wrote them where it parts from their ids'.
void lr_order_insertions(LrBuildTable& A, const LrItemDomain& dom,
                         const LrGraph& gr,
                         const std::vector<unsigned>& newId) {
  const LrBuckets<> mem(A.nstates, gr.states(),
                        [&](std::uint32_t s) { return newId[s]; });
  A.insertOf.row(0, A.nstates);
  LrInsertOrder order(A.cfg, dom, A.nullable, gr);
  std::unordered_map<std::vector<std::uint16_t>, std::uint32_t, VecHash>
      insertIx;
  std::vector<std::uint16_t> terms, list;
  std::vector<std::uint32_t> inRow(A.cfg.nterms, 0), inList(A.cfg.nterms, 0);
  for (unsigned f = 0; f < A.nstates; ++f) {
    const LrAction* const row = A.actions.data() + A.actionBase[f];
    terms.clear();
    for (unsigned z = 0; z < A.actionCount[f]; ++z)
      if (z == 0 || row[z].term != row[z - 1].term) {
        terms.push_back(row[z].term);
        inRow[row[z].term] = f + 1u;
      }
    if (terms.size() < 2) continue;
    order.of(mem.of(f), inRow, f + 1u, terms.size(), list);
    for (const std::uint16_t t : list) inList[t] = f + 1u;
    assert(std::ranges::is_sorted(terms));
    bool sorted = std::ranges::is_sorted(list);
    if (sorted && !list.empty())
      for (const std::uint16_t t : terms)
        if (inList[t] != f + 1u) {
          sorted = list.back() <= t;
          break;
        }
    if (sorted) continue;
    const auto [at, fresh] = insertIx.try_emplace(
        list, static_cast<std::uint32_t>(A.insertAt.size()));
    if (fresh) {
      A.insertWord.insert(A.insertWord.end(), list.begin(), list.end());
      A.insertAt.push_back(static_cast<std::uint32_t>(A.insertWord.size()));
    }
    A.insertOf[f] = at->second;
  }
}

// The graph, its merge and its folds, down to what the lexer reads: the
// state configurations and the modes. None of the graph outlives it.
template <bool WithLa>
std::optional<std::pair<std::vector<std::uint8_t>, LexModes>> lr_build_front(
    LrBuildTable& A, const LrLowered& low, const LexAlphabet& alpha,
    const LrBuildTable* against) {
  const auto& g = A.cfg;
  const LrBuckets<std::uint16_t> pn = lr_by_lhs(g);
  const LrItemDomain dom = lr_item_domain(g);
  const LrFirstSets fs = lr_first_sets(g);
  A.nullable = fs.nullable;
  const LrClosures cl = lr_closures(g, dom, pn, fs);

  LrGraph gr = lr_graph<WithLa>(A, dom, fs, cl);
  if (A.overflow) return std::nullopt;
  A.ncanonical = static_cast<std::uint16_t>(gr.states());
  std::vector<unsigned> newId;
  std::vector<unsigned> pick;  // final state -> representative old state
  const std::vector<DynSet> followers = lr_terminal_follow(g, fs);
  LexOvertakes<LrBuildCfg> overtakes(alpha.nfa, A.cfg, alpha.startOf,
                                     followers);
  std::vector<DynSet> flagged;
  {
    const LrLookback lb = lr_lookaheads(g, gr, fs, !WithLa);
    const LrActions actions{g, gr};

    // Under `core` no two states share a core: nothing to re-fuse.
    if (WithLa) {
      newId = lr_refuse(gr, actions);
      const auto number = lr_representatives<unsigned>(newId, pick);
      for (unsigned& id : newId) id = number[id];
    } else {
      pick.resize(gr.states());
      std::iota(pick.begin(), pick.end(), 0u);
      newId = pick;
    }
    A.nstates = static_cast<std::uint16_t>(pick.size());

    lr_elect_words(A);
    LrWrittenOrder order{g, dom, gr, cl, fs, pn, A};
    if (!lr_merge_entries(A, gr, actions, newId, lb, order)) return std::nullopt;
    if (!WithLa) flagged = lr_lex_flags(gr, lb, overtakes);
  }
  // What the wider graph is judged on is settled by the merge.
  if (against != nullptr && !lr_wider_stands(*against, A)) return std::nullopt;
  if (WithLa)
    flagged = lr_group_flags(gr, LrActions{g, gr}, newId, A.nstates, overtakes);
  lr_order_insertions(A, dom, gr, newId);
  if (!lr_fold_chains(A, low)) return std::nullopt;

  const LrPoolRows<LrItem> clos = gr.clos();
  lr_close_rows(A, clos, pick);
  auto cfgs = lr_state_cfgs(A, clos, pick, fs.first);
  const std::vector<DynSet> conditions = condition_firsts(A.cfg);
  auto modes = lr_lex_mode_sets(
      A, cfgs, lr_state_guard_firsts(A, clos, pick, conditions), conditions,
      flagged);
  return std::pair{std::move(cfgs), std::move(modes)};
}

template <bool WithLa>
LrBuildTable lr_build_impl(const LrLowered& low, LrBuildOptions opt,
                           const LrBuildTable* against = nullptr) {
  LrBuildTable A;
  A.identity = WithLa ? LrIdentity::core_plus_lookaheads : LrIdentity::core;
  A.cfg = low.cfg;
  A.writes = low.writes;
  if (A.writes.capped) A.cap_hit(kTooManyChains);
  if (!lr_guard_positions(A.cfg, A.writes))
    A.cap_hit(A.writes.capped ? kTooManyChains
                              : "it has more than 255 distinct conditions");

  const LexAlphabet alpha = lr_lex_alphabet(A);
  if (A.overflow || A.cfg.lowerFail()) return A;
  alpha.section_ends(A.cfg, lr_section_reads);

  const auto front = lr_build_front<WithLa>(A, low, alpha, against);
  if (!front) return A;
  const auto& [cfgs, modes] = *front;
  const std::vector<DynSet> wordAt = lr_lex_automaton(A, alpha, modes);
  if (A.overflow) return A;
  lr_state_symbols(A);
  if (opt.passes) lr_unit_elimination(A, modes, cfgs);
  lr_lex_candidates(A, alpha, modes, wordAt);
  if (A.overflow) return A;
  // Only a table that forks merges readings, and only a merge elects.
  if (A.nconflicts != 0 || A.multiCand) {
    lr_elect_runs(A);
  } else {
    A.electAt.clear();
    A.electWord.clear();
    A.electPos.clear();
  }
  if (opt.passes) {
    lr_minimize_states(A);
    // The last pass that moves cells or targets: a peek proves the table as
    // it stands.
    lr_peek_oracle(A);
  }
  if (!A.indented()) lr_subsume_modes(A, modes);
  lr_share_action_rows(A);
  if (A.overflow) return A;
  // Count the widest cell and conflicts after the state rewrites.
  // The merge's own conflict count already lets the peek oracle skip
  // unambiguous automata.
  A.maxArms = A.maxCands;
  A.nconflicts = 0;
  for (unsigned s = 0; s < A.nstates; ++s)
    lr_each_run(A.actions.data() + A.actionBase[s], A.actionCount[s],
                [&](std::size_t i, std::size_t j) {
                  if (j - i > A.maxArms)
                    A.maxArms = static_cast<std::uint16_t>(j - i);
                  A.nconflicts += static_cast<std::uint32_t>(j - i - 1);
                });
  return A;
}

}  // namespace

// `core` first, the cheaper graph: `nMergeInduced` says where the wider one is
// worth building.
LrBuildTable lr_build_automaton(const LrLowered& low, LrBuildOptions opt) {
  return lr_build_impl<false>(low, opt);
}

LrBuildTable lr_build_wide(const LrLowered& low, const LrBuildTable* against) {
  return lr_build_impl<true>(low, {}, against);
}

// Ambiguities, not cells: the passes copy a cell into every state they mint.
bool lr_wider_stands(const LrBuildTable& c, const LrBuildTable& w) {
  return !w.overflow &&
         (w.nOwed < c.nOwed || (c.nImpliedMixed != 0 && w.nOwed == c.nOwed));
}

}  // namespace cursiv
