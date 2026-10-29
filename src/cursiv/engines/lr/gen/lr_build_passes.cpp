// What a fused table is rewritten into before it is frozen: every pass moves
// where a reading is decided, never which readings there are.
#include "cursiv/engines/lr/gen/lr_build_passes.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cursiv/engines/lr/gen/LrBuckets.hpp"
#include "cursiv/engines/lr/gen/LrBuildCfg.hpp"
#include "cursiv/engines/lr/gen/LrGotoRows.hpp"
#include "cursiv/engines/lr/gen/LrLowProd.hpp"
#include "cursiv/engines/lr/gen/LrPeek.hpp"
#include "cursiv/engines/lr/gen/lr_term_run.hpp"
#include "cursiv/engines/lr/table/LrAction.hpp"
#include "cursiv/engines/lr/table/LrColdTables.hpp"
#include "cursiv/engines/shared/gen/bit_words.hpp"
#include "cursiv/engines/shared/gen/set_dedup.hpp"
#include "cursiv/engines/shared/table/Symbol.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {
namespace {

// The machine as it was, for a pass that mints while it chases; a row the
// array never reached reads zero.
template <class Arr>
auto lr_snap(const Arr& a, std::size_t n) {
  std::vector<std::remove_cvref_t<decltype(a.data()[0])>> out(n);
  const std::size_t k = a.size() < n ? a.size() : n;
  for (std::size_t i = 0; i < k; ++i) out[i] = a.data()[i];
  return out;
}

// A cell carried within one growing array, by value: the growth moves the
// array under a reference.
template <class Arr>
void lr_carry(Arr& a, std::size_t to, std::size_t from) {
  auto v = a[from];
  a[to] = std::move(v);
}

// A column whose cells states that fuse merge rather than agree on.
template <class Keep>
constexpr bool lr_merges = std::is_same_v<Keep, LrStateMerge>;

// What a pass composes for a state it mints: its rows, and its cell of each
// column whose policy is `LrStateComposed`.
struct LrComposedState {
  std::span<const LrAction> row;
  std::span<const LrGotoRows::Edge> gotos;
  DynSet owed;
};

// A state appended from what its pass composed; every other column takes
// `root`'s cell.
std::uint32_t lr_mint_state(LrBuildTable& A, LrComposedState made,
                            std::uint32_t root) {
  const std::uint32_t id = A.nstates;
  A.actionBase[id] = A.nactions;
  std::ranges::copy(made.row, A.actions.row(A.nactions, made.row.size()));
  A.nactions += static_cast<std::uint32_t>(made.row.size());
  A.actionCount[id] = static_cast<std::uint16_t>(made.row.size());
  A.gotoRows.push_row(made.gotos);
  A.each_state_column([&](auto& column, auto keep) {
    if constexpr (!std::is_same_v<decltype(keep), LrStateComposed>)
      lr_carry(column, id, root);
  });
  A.closeRow[id] = std::move(made.owed);
  A.nstates = static_cast<std::uint16_t>(id + 1);
  return id;
}

// Per state, one word for each column states that fuse must agree on, `width`
// of them: the cell, or its rank among the column's distinct sets.
std::vector<std::uint32_t> lr_kept_cells(LrBuildTable& A, std::uint32_t n,
                                         unsigned& width) {
  width = 0;
  A.each_state_column([&](auto&, auto keep) {
    if constexpr (!lr_merges<decltype(keep)>) ++width;
  });
  std::vector<std::uint32_t> out(std::size_t{n} * width);
  std::vector<std::uint16_t> rank(n);
  unsigned c = 0;
  A.each_state_column([&]<class T>(Vec<T>& column, auto keep) {
    if constexpr (!lr_merges<decltype(keep)>) {
      // A column no pass laid reads empty cells.
      const T* const cell = column.row(0, n);
      if constexpr (std::is_same_v<T, DynSet>) set_dedup(cell, n, rank.data());
      for (std::uint32_t s = 0; s < n; ++s) {
        if constexpr (std::is_same_v<T, DynSet>)
          out[std::size_t{s} * width + c] = rank[s];
        else
          out[std::size_t{s} * width + c] = cell[s];
      }
      ++c;
    }
  });
  return out;
}

// The states the start reaches through shifts and gotos.
std::vector<std::uint8_t> lr_live_states(const LrBuildTable& A,
                                         const LrGotoRows& go) {
  std::vector<std::uint8_t> lv(A.nstates, 0);
  std::vector<std::uint32_t> work{0};
  lv[0] = 1;
  const auto reach = [&](std::uint32_t t) {
    if (!lv[t]) {
      lv[t] = 1;
      work.push_back(t);
    }
  };
  while (!work.empty()) {
    const std::uint32_t s = work.back();
    work.pop_back();
    for (const LrAction& x : A.action_row(s)) A.each_landing(x, reach);
    for (const LrGotoRows::Edge& e : go[s]) A.each_goto_landing(e, reach);
  }
  return lv;
}

}  // namespace

void lr_share_action_rows(LrBuildTable& A) {
  const LrAction* const acts = A.actions.data();
  std::unordered_map<std::vector<std::uint64_t>, std::uint32_t, VecHash> rows;
  std::vector<LrAction> actions;
  std::vector<std::uint32_t> bases(A.nstates);
  for (unsigned s = 0; s < A.nstates; ++s) {
    const auto base = A.actionBase[s];
    const unsigned count = A.actionCount[s];
    std::vector<std::uint64_t> key;
    key.reserve(2 * count);
    for (unsigned i = 0; i < count; ++i) {
      const LrAction& x = acts[base + i];
      key.push_back((std::uint64_t{x.term} << 18) |
                    (std::uint64_t{x.arg} << 2) |
                    static_cast<std::uint64_t>(x.kind));
      key.push_back((std::uint64_t{x.exact} << 32) |
                    (std::uint64_t{x.site} << 16) | x.outer);
    }
    const auto [at, fresh] = rows.try_emplace(
        std::move(key), static_cast<std::uint32_t>(actions.size()));
    bases[s] = at->second;
    if (!fresh || count == 0) continue;
    actions.insert(actions.end(), acts + base, acts + base + count);
  }
  A.nactions = static_cast<std::uint32_t>(actions.size());
  A.actions.v = std::move(actions);
  A.actionBase.v = std::move(bases);
}

// `a` covers `b` when b's set is inside a's and their trivia agree. A token
// b flags as hiding a rival is still owed b's walk: its reader re-lexes it.
void lr_subsume_modes(LrBuildTable& A, const LexModes& modes) {
  const std::uint32_t nm = A.nmodes;
  const std::size_t subWords = (std::size_t{nm} * nm + 63u) / 64u;
  std::uint64_t* const sub = A.modeSub.row(0, subWords);
  for (std::size_t w = 0; w < subWords; ++w) sub[w] = 0;
  modes.cover(true, [&](std::size_t a, std::size_t b) {
    const std::size_t i = a * nm + b;
    sub[i >> 6] |= std::uint64_t{1} << (i & 63u);
  });
}

// A conflict cell the second token decides needs no fork: each arm consumes
// the cell's terminal -- a reduce through its lhs's whole goto row, the safe
// over-approximation -- and collects what the states after the shift accept.
// Two arms with disjoint sets, each under one mode, are decided by a peek.
namespace {

// The actions a walk has pushed, stamped rather than cleared.
struct LrPeekWalk {
  std::vector<std::uint32_t> seen;
  std::vector<std::uint32_t> wl;
  std::uint32_t gen = 0;

  void open(std::uint32_t nactions) {
    if (seen.size() < nactions) seen.resize(nactions, 0);
    if (++gen == 0) {
      std::ranges::fill(seen, 0u);
      gen = 1;
    }
    wl.clear();
  }
  void push(std::uint32_t i) {
    if (seen[i] == gen) return;
    seen[i] = gen;
    wl.push_back(i);
  }
};

// One arm of a peeked cell, its next tokens folded into `tok2` and its mode
// pinned to one. False where a peek may not stand on it.
bool lr_peek_arm(const LrBuildTable& A, const LrGotoColumns& go,
                 LrPeekWalk& walk, std::uint32_t ai, std::uint16_t t,
                 std::uint16_t cellMode, std::uint16_t& mode, DynSet& tok2) {
  walk.open(A.nactions);
  std::vector<std::uint32_t>& wl = walk.wl;
  walk.push(ai);
  while (!wl.empty()) {
    const std::uint32_t at = wl.back();
    wl.pop_back();
    const LrAction& x = A.actions[at];
    if (x.kind == LrActionKind::shift) {
      const std::uint32_t w = x.arg;
      if (w >= A.nstates) return false;
      const std::uint16_t wm = A.mode[w];
      if (A.modeMulti[wm] != 0) return false;
      if (A.flags_mode(wm)) return false;
      if (mode == 0xffffu)
        mode = A.mode[w];
      else if (mode != A.mode[w])
        return false;
      // An implied token is an insertion the peek cannot see.
      for (unsigned u = 1; u < A.cfg.nterms; ++u)
        if (A.cfg.terms[u].laid == kLaidImplied && A.follow[w].test(u))
          return false;
      tok2 |= A.follow[w];
    } else if (x.kind == LrActionKind::reduce) {
      // A reduce the gap decides is not one the next token decides.
      if (A.cfg.prods[x.arg].gap != 0) return false;
      const unsigned lhs = A.cfg.prods[x.arg].lhs;
      for (const auto e : go[lhs]) {
        // ...nor is a goto the gap decides.
        if (e.guard != 0) return false;
        const std::uint32_t u = e.to;
        if (A.mode[u] != cellMode) return false;
        const auto [lo, n] = lr_term_run(A.actions.data() + A.actionBase[u],
                                         A.actionCount[u], t);
        if (n == 0) continue;  // this context freezes: it adds nothing
        if (n > 1) return false;
        walk.push(A.actionBase[u] + lo);
      }
    } else {
      return false;  // accept arbitrates nothing
    }
  }
  return true;
}

// Both arms of the cell at `at`, read into `pk`.
bool lr_peek_arms(const LrBuildTable& A, const LrGotoColumns& go,
                  LrPeekWalk& walk, std::uint32_t at, LrPeek& pk) {
  const std::uint16_t cellMode = A.mode[pk.state];
  return lr_peek_arm(A, go, walk, at, pk.term, cellMode, pk.mode[0],
                     pk.set[0]) &&
         lr_peek_arm(A, go, walk, at + 1, pk.term, cellMode, pk.mode[1],
                     pk.set[1]);
}

}  // namespace

void lr_peek_oracle(LrBuildTable& A) {
  if (A.nconflicts == 0) return;
  LrPeekWalk walk;
  const LrGotoColumns go(A.gotoRows, A.cfg.nnts);
  for (std::uint32_t s = 0; s < A.nstates; ++s) {
    const std::uint32_t b = A.actionBase[s];
    lr_each_run(A.actions.data() + b, A.actionCount[s],
                [&](std::size_t i, std::size_t j) {
                  LrPeek pk{.state = static_cast<std::uint16_t>(s),
                            .term = A.actions[b + i].term};
                  if (j - i == 2 &&
                      lr_peek_arms(A, go, walk,
                                   static_cast<std::uint32_t>(b + i), pk) &&
                      pk.mode[0] != 0xffffu && pk.mode[1] != 0xffffu &&
                      !pk.set[0].intersects(pk.set[1]))
                    A.peeks[A.npeeks++] = pk;
                });
  }
}

std::vector<std::uint16_t> lr_shift_terms(const LrBuildTable& a) {
  std::vector terms(a.nstates, kInvalidSymbol);
  for (unsigned s = 0; s < a.nstates; ++s)
    for (const LrAction& x : a.action_row(s)) {
      const auto stamp = [&](std::uint16_t to) {
        auto& term = terms[to];
        if (term != kInvalidSymbol && term != x.term)
          throw std::logic_error(
              "LR: a shifted state lost its terminal identity");
        term = x.term;
      };
      a.each_landing(x, stamp);
    }
  return terms;
}

void lr_reduce_key(const LrBuildCfg& g, unsigned p,
                   std::vector<std::uint32_t>& key) {
  const LrLowProd& pr = g.prods[p];
  const LrProdBuild& pb = g.prodBuild[p];
  key = {pr.lhs,
         pr.len,
         pr.node,
         pb.owner,
         static_cast<std::uint32_t>(pb.tailCfg) |
             (static_cast<std::uint32_t>(pr.gap) << 8),
         static_cast<std::uint32_t>(pb.endOnly)};
  for (unsigned i = 0; i < pr.len; ++i) {
    const std::uint16_t sy = g.sym_at(pr, i);
    const auto row = g.row_at(pr, i);
    key.push_back(row);
    key.push_back(row == kNoRow ? 0u
                  : is_term(sy) ? g.terms[sy].element
                                : 0xffffffffu);
  }
}

// Two states with the same rows -- actions, gotos, and every column the
// states that fuse keep -- towards the same classes are one: partition
// refinement to the fixpoint, then one representative per class, the
// unreachable ones swept. A tag is kept as the runtime reads it, held unless
// known false: no reader past this pass tells a known truth from no knowledge.
void lr_minimize_states(LrBuildTable& A) {
  for (std::size_t s = 0; s < A.tagKnown.size(); ++s) {
    A.tagKnown[s].andnot(A.tagTruth[s]);
    A.tagTruth[s] = {};
  }
  const auto shiftTerm = lr_shift_terms(A);
  const auto& g = A.cfg;
  const std::uint32_t n = A.nstates;
  const std::uint32_t* const abase = A.actionBase.data();
  const std::uint16_t* const acount = A.actionCount.data();
  unsigned width = 0;
  const std::vector<std::uint32_t> held = lr_kept_cells(A, n, width);
  const LrAction* const acts = A.actions.data();
  const LrGotoRows& go = A.gotoRows;
  // A guard is read by the classes it lands in.
  std::vector guardCls(A.guards.size(), std::uint32_t{0});
  const std::vector<std::uint8_t> live = lr_live_states(A, go);
  const std::uint8_t* const lv = live.data();

  // Reductions are equivalent by their fixed grammar data. Their lhs is a
  // nonterminal id, never a state class, so this partition is computed once.
  std::vector<std::uint32_t> pc(g.nprods);
  {
    std::unordered_map<std::vector<std::uint32_t>, std::uint32_t, VecHash> ids;
    std::vector<std::uint32_t> key;
    for (std::uint32_t p = 0; p < g.nprods; ++p) {
      lr_reduce_key(g, p, key);
      pc[p] = ids.try_emplace(key, static_cast<std::uint32_t>(ids.size()))
                  .first->second;
    }
  }

  // The first partition holds each row's fixed data. Later keys retain
  // that class and compare only its destinations: refinement only splits.
  std::vector<std::uint32_t> cls(n, 0), next(n);
  std::uint32_t nclasses = 1;
  std::vector<std::uint32_t> key;
  std::vector<std::uint64_t> gotos;
  for (;;) {
    const bool initial = nclasses == 1;
    std::vector<unsigned> size(nclasses);
    for (std::uint32_t s = 0; s < n; ++s)
      if (lv[s]) ++size[cls[s]];
    // After the first round only a changed successor can split a class.
    // `next` still holds the preceding partition until rows are written.
    std::vector<char> dirty(nclasses, initial), represented(nclasses);
    for (std::uint32_t s = 0; s < n; ++s) {
      if (!lv[s] || size[cls[s]] == 1 || dirty[cls[s]]) continue;
      const auto moved = [&](std::uint16_t to) {
        dirty[cls[s]] = dirty[cls[s]] || cls[to] != next[to];
      };
      for (const LrAction& x : A.action_row(s)) A.each_landing(x, moved);
      for (const auto& e : go[s]) A.each_goto_landing(e, moved);
    }
    auto count = nclasses;
    std::unordered_map<std::vector<std::uint32_t>, std::uint32_t, VecHash>
        guards, states;
    states.reserve(n);
    for (std::size_t q = 0; q < A.guards.size(); ++q) {
      lr_guard_key(A.guards[q], [&](std::uint16_t to) { return cls[to]; }, key);
      guardCls[q] =
          guards.try_emplace(key, static_cast<std::uint32_t>(guards.size()))
              .first->second;
    }
    for (std::uint32_t s = 0; s < n; ++s) {
      if (!lv[s]) continue;
      if (size[cls[s]] == 1 || !dirty[cls[s]]) {
        next[s] = cls[s];
        continue;
      }
      key.assign(1, cls[s]);
      if (initial) {
        key.push_back(shiftTerm[s]);
        const auto cells =
            held.begin() + static_cast<std::ptrdiff_t>(s) * width;
        key.insert(key.end(), cells, cells + width);
      }
      for (unsigned i = 0, c = acount[s]; i < c; ++i) {
        const LrAction& x = acts[abase[s] + i];
        if (!initial && x.kind != LrActionKind::shift &&
            x.kind != LrActionKind::guard)
          continue;
        if (initial) {
          key.push_back(x.term);
          key.push_back(static_cast<std::uint32_t>(x.kind));
        }
        key.push_back(x.kind == LrActionKind::shift ? 0x80000000u | cls[x.arg]
                      : x.kind == LrActionKind::guard
                          ? 0x20000000u | guardCls[x.arg]
                          : 0x40000000u | pc[x.arg]);
        if (initial) {
          key.push_back(static_cast<std::uint32_t>(x.site));
          key.push_back(static_cast<std::uint32_t>(x.outer));
          key.push_back(x.exact ? 1u : 0u);
        }
      }
      gotos.clear();
      for (const LrGotoRows::Edge& e : go[s])
        gotos.push_back(
            (static_cast<std::uint64_t>(e.nt) << 33) |
            (e.guard == 0
                 ? static_cast<std::uint64_t>(cls[e.to]) << 1
                 : (static_cast<std::uint64_t>(guardCls[e.guard - 1u]) << 1) |
                       1u));
      std::ranges::sort(gotos);
      const auto unique = std::ranges::unique(gotos);
      for (auto it = gotos.begin(); it != unique.begin(); ++it) {
        key.push_back(0xC0000000u | static_cast<std::uint32_t>(*it >> 32));
        key.push_back(static_cast<std::uint32_t>(*it));
      }
      const auto [at, fresh] = states.try_emplace(key, cls[s]);
      if (fresh && std::exchange(represented[cls[s]], 1)) at->second = count++;
      next[s] = at->second;
    }
    cls.swap(next);
    if (count == nclasses) break;
    nclasses = count;
  }
  // One representative per class, numbered by first occurrence so the start
  // state keeps its seat.
  std::vector<std::uint32_t> reps;
  reps.reserve(nclasses);
  const auto newIdx = lr_representatives(cls, reps, live);
  const auto remap = [&](std::uint32_t s) {
    return static_cast<std::uint16_t>(newIdx[cls[s]]);
  };
  const auto k = static_cast<std::uint32_t>(reps.size());
  const std::uint32_t* const rp = reps.data();

  if (k < n) {
    // The survivors are written over the rows they come from: first
    // occurrences ascend, so the copy runs left.
    LrAction* const wact = A.actions.row(0, A.nactions);
    std::uint32_t nna = 0;

    const LrAction* const oact = A.actions.data();
    std::uint32_t* const wab = A.actionBase.row(0, k);
    std::uint16_t* const wac = A.actionCount.row(0, k);
    for (std::uint32_t d = 0; d < k; ++d) {
      const std::uint32_t r = rp[d];
      const std::uint32_t ab = nna;
      const std::uint32_t rab = abase[r];
      for (unsigned i = 0, ac = acount[r]; i < ac; ++i) {
        LrAction x = oact[rab + i];
        if (x.kind == LrActionKind::shift) x.arg = remap(x.arg);
        wact[nna++] = x;
      }
      wab[d] = ab;
      wac[d] = static_cast<std::uint16_t>(nna - ab);
    }
    A.nactions = nna;

    A.each_state_column([&]<class T>(Vec<T>& column, auto keep) {
      if constexpr (lr_merges<decltype(keep)>) {
        // A column no pass laid stays unlaid.
        if (column.size() < n) return;
        std::vector<T> cell(k, keep.none);
        std::vector<std::uint8_t> seen(k, 0);
        for (std::uint32_t s = 0; s < n; ++s) {
          if (!lv[s]) continue;
          const std::uint16_t d = remap(s);
          if (!seen[d]) {
            seen[d] = 1;
            cell[d] = column[s];
          } else if (cell[d] != column[s]) {
            cell[d] = keep.none;
          }
        }
        for (std::uint32_t d = 0; d < k; ++d) column[d] = cell[d];
      } else {
        // Each representative's cell is read once, before it is written over.
        for (std::uint32_t d = 0; d < k; ++d)
          if (rp[d] != d) column[d] = std::move(column[rp[d]]);
      }
      column.truncate(k);
    });
    A.each_state_ref([&](std::uint16_t& s, std::uint16_t none) {
      if (s != none) s = lv[s] ? remap(s) : none;
    });
    LrGotoRows kept;
    for (std::uint32_t d = 0; d < k; ++d) {
      for (LrGotoRows::Edge e : go[rp[d]]) {
        e.to = remap(e.to);
        kept.edges.push_back(e);
      }
      kept.close_row();
    }
    // The programs the survivors still name, each once, in their order.
    std::vector<std::uint8_t> used(A.guards.size(), 0);
    for (std::uint32_t i = 0; i < nna; ++i)
      if (wact[i].kind == LrActionKind::guard) used[wact[i].arg] = 1;
    for (const LrGotoRows::Edge& e : kept.edges)
      if (e.guard != 0) used[e.guard - 1u] = 1;
    std::unordered_map<std::vector<std::uint32_t>, std::uint32_t, VecHash> ids;
    std::vector<std::uint32_t> program(A.guards.size());
    std::vector<LrGuard> programs;
    for (std::size_t q = 0; q < A.guards.size(); ++q) {
      if (!used[q]) continue;
      lr_guard_key(A.guards[q], [](std::uint16_t to) { return to; }, key);
      const auto [at, fresh] =
          ids.try_emplace(key, static_cast<std::uint32_t>(programs.size()));
      if (fresh) programs.push_back(std::move(A.guards[q]));
      program[q] = at->second;
    }
    for (std::uint32_t i = 0; i < nna; ++i)
      if (wact[i].kind == LrActionKind::guard)
        wact[i].arg = static_cast<std::uint16_t>(program[wact[i].arg]);
    for (LrGotoRows::Edge& e : kept.edges)
      if (e.guard != 0) e.guard = program[e.guard - 1u] + 1u;
    A.guards = std::move(programs);
    A.set_gotos(std::move(kept));
    A.nstates = static_cast<std::uint16_t>(k);
    A.actionBase.truncate(A.nstates);
    A.actionCount.truncate(A.nstates);
    A.actions.truncate(A.nactions);
  }
}

// What a state owes: the fewest terminals a kernel item still needs, a list's
// continuation aside, and the first terminals of those shortest completions.
// An item set aside counts where it shifts one of them: every item taking a
// due shift then owes one token less.
void lr_close_rows(LrBuildTable& A, const LrPoolRows<LrItem>& clos,
                   const std::vector<unsigned>& pick) {
  const auto& g = A.cfg;
  const LrLowProd* const prods = g.prods.data();
  const unsigned nN = g.nnts;
  const unsigned nP = g.nprods;
  constexpr std::uint64_t kInf = ~std::uint64_t{0};
  const auto add = [](std::uint64_t a, std::uint64_t b) {
    return a > kInf - b ? kInf : a + b;
  };
  std::vector yieldN(nN, kInf);
  const auto yieldSym = [&](std::uint16_t x) {
    return is_term(x) ? std::uint64_t{1} : yieldN[nt_index(x)];
  };
  const auto yieldFrom = [&](const LrLowProd& p, unsigned i) {
    std::uint64_t s = 0;
    for (; i < p.len; ++i) s = add(s, yieldSym(g.sym_at(p, i)));
    return s;
  };
  for (bool changed = true; changed;) {
    changed = false;
    for (unsigned p = 0; p < nP; ++p) {
      const std::uint64_t s = yieldFrom(prods[p], 0);
      if (s < yieldN[prods[p].lhs]) {
        yieldN[prods[p].lhs] = s;
        changed = true;
      }
    }
  }
  std::vector<DynSet> firstN(nN);
  const auto firstFrom = [&](const LrLowProd& p, unsigned i, DynSet& out) {
    for (; i < p.len; ++i) {
      const std::uint16_t x = g.sym_at(p, i);
      if (yieldSym(x) == 0) continue;
      if (is_term(x))
        out.set(x);
      else
        out |= firstN[nt_index(x)];
      return;
    }
  };
  for (bool changed = true; changed;) {
    changed = false;
    for (unsigned p = 0; p < nP; ++p) {
      const LrLowProd& pr = prods[p];
      if (yieldN[pr.lhs] == kInf || yieldFrom(pr, 0) != yieldN[pr.lhs])
        continue;
      DynSet grown = firstN[pr.lhs];
      firstFrom(pr, 0, grown);
      if (grown == firstN[pr.lhs]) continue;
      firstN[pr.lhs] = std::move(grown);
      changed = true;
    }
  }
  A.closeMax = 0;
  for (unsigned p = 0; p < nP; ++p)
    if (const std::uint64_t y = yieldFrom(prods[p], 0); y != kInf)
      A.closeMax = std::max(A.closeMax, y);

  const auto owed = [&](const LrItem& it) {
    const LrLowProd& p = prods[it.prod];
    if (it.dot == 0 && it.prod != g.startProd) return kInf;
    if (const std::uint16_t head = p.len != 0 ? g.sym_at(p, 0) : kNtBase;
        it.dot == 1 && !is_term(head) && nt_index(head) == p.lhs)
      return kInf;
    return yieldFrom(p, it.dot);
  };
  DynSet implied;
  for (unsigned t = 0; t < g.nterms; ++t)
    if (g.terms[t].laid == kLaidImplied) implied.set(t);
  for (unsigned f = 0; f < A.nstates; ++f) {
    const std::span<const LrItem> items = clos[pick[f]];
    std::uint64_t owe = kInf;
    for (const LrItem& it : items) owe = std::min(owe, owed(it));
    DynSet row;
    if (owe != 0 && owe != kInf)
      for (const LrItem& it : items)
        if (owed(it) == owe) firstFrom(prods[it.prod], it.dot, row);
    row.andnot(implied);
    const DynSet due = row;
    const auto counted = [&](const LrItem& it) {
      const std::uint64_t o = owed(it);
      const LrLowProd& p = prods[it.prod];
      if (o != kInf || it.dot >= p.len) return o;
      const std::uint16_t t = g.sym_at(p, it.dot);
      return is_term(t) && due.test(t) ? yieldFrom(p, it.dot) : kInf;
    };
    std::uint64_t least = owe;
    for (const LrItem& it : items) least = std::min(least, counted(it));
    if (least < owe) {
      owe = least;
      row = DynSet{};
      for (const LrItem& it : items)
        if (counted(it) == owe) row.set(g.sym_at(prods[it.prod], it.dot));
    }
#ifndef NDEBUG
    // A due terminal lands where one token less is owed.
    for (const LrItem& it : items) {
      const LrLowProd& p = prods[it.prod];
      if (counted(it) != owe || owe == 0 || owe == kInf || it.dot >= p.len)
        continue;
      const std::uint16_t t = g.sym_at(p, it.dot);
      if (!is_term(t) || !row.test(t)) continue;
      std::uint64_t next = kInf;
      for (const LrItem& o : items)
        if (o.dot < prods[o.prod].len && g.sym_at(prods[o.prod], o.dot) == t)
          next = std::min(next, yieldFrom(prods[o.prod], o.dot + 1u));
      assert(next + 1 == owe && "a due shift owes one token less");
    }
#endif
    A.closeRow[f] = std::move(row);
  }
}

bool lr_unit_passes(const LrBuildCfg& g, unsigned p) {
  const LrLowProd& pr = g.prods[p];
  const LrProdBuild& pb = g.prodBuild[p];
  return pr.len == 1 && pr.node == kNoRow && g.row_at(pr, 0) == kNoRow &&
         g.cfg_at(static_cast<std::uint16_t>(p), 0) == 0 && pb.tailCfg == 0 &&
         !pb.endOnly && p != g.startProd && pr.gap == 0;
}

// A pass-through reduce is dispatch, not work: every edge into such a state is
// retargeted to a copy answering what the successor answers. A cell stays
// where the substitution cannot be proven exact.
void lr_unit_elimination(LrBuildTable& A, const LexModes& modes,
                         const std::vector<std::uint8_t>& cfgS) {
  const auto& g = A.cfg;
  const LrLowProd* const prods = g.prods.data();
  const unsigned nN = g.nnts;
  const unsigned nP = g.nprods;
  const std::uint32_t n0 = A.nstates;
  const auto shiftTerm = lr_shift_terms(A);
  std::vector<std::uint8_t> elig(nP, 0);
  for (unsigned p = 0; p < nP; ++p) elig[p] = lr_unit_passes(g, p);
  // Every composition reads the machine as built.
  const std::vector<LrAction> sAct = lr_snap(A.actions, A.nactions);
  const std::vector<std::uint32_t> sAb = lr_snap(A.actionBase, n0);
  const std::vector<std::uint16_t> sAc = lr_snap(A.actionCount, n0);
  const LrAction* const sa = sAct.data();
  const std::uint32_t* const ab = sAb.data();
  const std::uint16_t* const ac = sAc.data();
  LrGotoRows out = A.gotoRows;

  // Which states hold an eligible cell, outside any conflict run.
  std::vector<std::uint8_t> hasE(n0, 0);
  for (std::uint32_t s = 0; s < n0; ++s) {
    const LrAction* const row = sa + ab[s];
    lr_each_run(row, ac[s], [&](std::size_t i, std::size_t j) {
      if (j - i == 1 && row[i].kind == LrActionKind::reduce && elig[row[i].arg])
        hasE[s] = 1;
    });
  }
  if (std::ranges::any_of(hasE, [](std::uint8_t x) { return x != 0; })) {
    // A substituted cell reads the same token where the successor's
    // alphabet is within this one's, under the same configuration.
    const auto wantWithin = [&](std::uint32_t u, std::uint32_t x) {
      return cfgS[u] == cfgS[x] && modes.covers(A.mode[x], A.mode[u]);
    };

    // Index only the original edges, before any copy is appended. Columns
    // ascend by nonterminal/source: targetOf's mint order numbers states.
    const LrGotoColumns columns(out, nN);
    using UpeEdge = LrGotoRows::Edge;
    std::vector<std::vector<LrAction>> mRow;  // as composed, before rewrites
    std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> mByHash;
    std::vector<std::uint32_t> work;
    std::vector<std::uint32_t> gStamp(nN, 0), gPos(nN, 0);
    std::uint32_t gGen = 0;

    // A guarded goto has no one successor to chase.
    const auto gotoSnap = [&](std::uint32_t from, unsigned nt) {
      const std::span<const UpeEdge> row = out[from];
      if (from < n0) {
        const auto it = std::ranges::lower_bound(row, nt, {}, &UpeEdge::nt);
        return it != row.end() && it->nt == nt && it->guard == 0
                   ? std::uint32_t{it->to}
                   : 0xFFFFFFFFu;
      }
      for (const UpeEdge& e : row)
        if (e.nt == nt) return e.guard == 0 ? std::uint32_t{e.to} : 0xFFFFFFFFu;
      return 0xFFFFFFFFu;
    };

    // What a composition read of `F`'s gotos, and what it answered: any state
    // whose gotos agree on those reads composes the same copy.
    struct Memo {
      std::vector<std::pair<std::uint16_t, std::uint32_t>> reads;
      std::uint32_t id = 0;
      bool unminted = false;
    };
    std::vector<std::vector<Memo>> memo(n0);
    std::vector<std::pair<std::uint16_t, std::uint32_t>> reads;
    std::vector<std::uint32_t> readAt(nN, 0);
    std::uint32_t readGen = 0;
    bool unminted = false;

    // The copy of `X` whose substituted cells answer through the gotos of `F`.
    const auto compose = [&](std::uint32_t F, std::uint32_t X) {
      std::vector<LrAction> row;
      const std::span<const UpeEdge> xOut = out[X];
      std::vector<UpeEdge> gadd(xOut.begin(), xOut.end());
      ++gGen;
      const auto gSeat = [&](std::size_t i) {
        const unsigned nt = gadd[i].nt;
        if (gStamp[nt] == gGen) return;
        gStamp[nt] = gGen;
        gPos[nt] = static_cast<std::uint32_t>(i);
      };
      const auto gAt = [&](unsigned nt) {
        return gStamp[nt] == gGen ? static_cast<int>(gPos[nt]) : -1;
      };
      for (std::size_t i = 0; i < gadd.size(); ++i) gSeat(i);
      std::uint32_t subs = 0;
      const LrAction* const xrow = sa + ab[X];
      const unsigned xc = ac[X];
      // A substituted cell owes what its successor owes.
      DynSet owed;
      lr_each_run(xrow, xc, [&](std::size_t i, std::size_t j) {
        bool done = false;
        if (j - i == 1 && xrow[i].kind == LrActionKind::reduce &&
            elig[xrow[i].arg]) {
          const std::uint16_t a = xrow[i].term;
          unsigned pp = xrow[i].arg;
          std::uint32_t fin = 0xFFFFFFFFu;
          unsigned fb = 0, fe = 0;
          for (unsigned d = 0; d < nN; ++d) {
            const unsigned lhs = prods[pp].lhs;
            const std::uint32_t u = gotoSnap(F, lhs);
            if (readAt[lhs] != readGen) {
              readAt[lhs] = readGen;
              reads.emplace_back(static_cast<std::uint16_t>(lhs), u);
            }
            if (u == 0xFFFFFFFFu) break;
            if (!wantWithin(u, X)) break;
            const LrAction* const ur = sa + ab[u];
            const auto [lo, alen] = lr_term_run(ur, ac[u], a);
            if (alen == 0) {
              // The narrower successor can re-lex this token: the reduce stays.
              break;
            }
            const unsigned hi = lo + alen;
            if (alen == 1 && ur[lo].kind == LrActionKind::reduce &&
                elig[ur[lo].arg]) {
              pp = ur[lo].arg;
              continue;
            }
            fin = u;
            fb = ab[u] + lo;
            fe = ab[u] + hi;
            break;
          }
          if (fin != 0xFFFFFFFFu) {
            // The successor's goto rows join, unless one disagrees with a
            // seated row: then the cell stays a reduce.
            bool clash = false;
            for (const UpeEdge& e : out[fin]) {
              const int at = gAt(e.nt);
              if (at >= 0 &&
                  (gadd[at].to != e.to || gadd[at].guard != e.guard)) {
                clash = true;
                break;
              }
            }
            if (!clash) {
              for (unsigned k = fb; k < fe; ++k) row.push_back(sa[k]);
              if (A.closeRow[fin].test(a)) owed.set(a);
              for (const UpeEdge& e : out[fin])
                if (gAt(e.nt) < 0) {
                  gadd.push_back(e);
                  gSeat(gadd.size() - 1);
                }
              ++subs;
              done = true;
            }
          }
        }
        if (!done) {
          for (std::size_t k = i; k < j; ++k) row.push_back(xrow[k]);
          if (A.closeRow[X].test(xrow[i].term)) owed.set(xrow[i].term);
        }
      });
      if (subs == 0) return X;

      std::uint64_t h = hash_mix(1469598103934665603ull, X);
      for (const LrAction& x : row) {
        h = hash_mix(h, static_cast<std::uint64_t>(x.term) |
                            (static_cast<std::uint64_t>(x.arg) << 16) |
                            (static_cast<std::uint64_t>(x.kind) << 32));
        h = hash_mix(h, static_cast<std::uint64_t>(x.site) |
                            (static_cast<std::uint64_t>(x.outer) << 16));
      }
      for (const UpeEdge& e : gadd)
        h = hash_mix(h, static_cast<std::uint64_t>(e.nt) |
                            (static_cast<std::uint64_t>(e.to) << 16));
      for (const std::uint32_t m : mByHash[h]) {
        const std::uint32_t id = n0 + m;
        if (row == mRow[m] && std::ranges::equal(out[id], gadd) &&
            A.closeRow[id] == owed)
          return id;
      }
      // A copy the state index cannot name is not minted.
      if (A.nstates >= 0xFFFFu || row.size() >= 0xffffu) {
        ++A.nunminted;
        unminted = true;
        return X;
      }
      const std::uint32_t id =
          lr_mint_state(A, {row, gadd, std::move(owed)}, X);
      out.push_row(gadd);
      mRow.push_back(std::move(row));
      mByHash[h].push_back(id - n0);
      work.push_back(id);
      return id;
    };

    // One retargeted edge (F -> X): hop the whole-state pass-throughs, then
    // compose a copy if eligible cells remain.
    const auto targetOf = [&](std::uint32_t F, std::uint32_t X) {
      for (unsigned d = 0; d < nN; ++d) {
        if (X >= n0 || !hasE[X]) return X;
        // A shifted state keeps its terminal identity: its cells are composed
        // below instead.
        if (shiftTerm[X] != kInvalidSymbol) break;
        const LrAction* const row = sa + ab[X];
        const unsigned c = ac[X];
        int p = -1;
        bool whole = c != 0;
        for (unsigned i = 0; i < c && whole; ++i) {
          if (row[i].kind != LrActionKind::reduce || !elig[row[i].arg] ||
              (p >= 0 && row[i].arg != p))
            whole = false;
          else
            p = row[i].arg;
        }
        if (!whole) break;
        const std::uint32_t u = gotoSnap(F, prods[p].lhs);
        if (u == 0xFFFFFFFFu || u == X) break;
        if (!wantWithin(u, X)) break;
        X = u;
      }
      if (X >= n0 || !hasE[X]) return X;
      for (const Memo& m : memo[X]) {
        if (!std::ranges::all_of(m.reads, [&](const auto& r) {
              return gotoSnap(F, r.first) == r.second;
            }))
          continue;
        A.nunminted += m.unminted;
        return m.id;
      }
      reads.clear();
      ++readGen;
      unminted = false;
      const std::uint32_t id = compose(F, X);
      memo[X].emplace_back(reads, id, unminted);
      return id;
    };

    // Every edge, the minted states' as they come. `targetOf` mints and moves
    // the arrays: each cell is named through the store.
    const auto retarget_shifts = [&](std::uint32_t s) {
      for (unsigned i = 0, c = A.actionCount[s]; i < c; ++i) {
        const std::uint32_t k = A.actionBase[s] + i;
        if (A.actions[k].kind != LrActionKind::shift) continue;
        const auto to =
            static_cast<std::uint16_t>(targetOf(s, A.actions[k].arg));
        A.actions[k].arg = to;
      }
    };
    for (std::uint32_t s = 0; s < n0; ++s) retarget_shifts(s);
    for (unsigned nt = 0; nt < nN; ++nt)
      for (const auto e : columns[nt]) {
        if (e.guard != 0) continue;
        const auto to = static_cast<std::uint16_t>(targetOf(e.from, e.to));
        A.gotoRows.edges[e.index].to = to;
      }
    for (std::size_t wi = 0; wi < work.size(); ++wi) {
      const std::uint32_t s = work[wi];
      retarget_shifts(s);
      const auto end = A.gotoRows.base[s + 1];
      for (std::uint32_t j = A.gotoRows.base[s]; j < end; ++j) {
        if (A.gotoRows.edges[j].guard != 0) continue;
        const auto to =
            static_cast<std::uint16_t>(targetOf(s, A.gotoRows.edges[j].to));
        A.gotoRows.edges[j].to = to;
      }
    }
    // New rows must keep composition order until every edge is retargeted.
    A.set_gotos(std::move(A.gotoRows));
  }
}

}  // namespace cursiv
