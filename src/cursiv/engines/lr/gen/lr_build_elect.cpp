// A left recursion reads as a descent reads its fold: its base first, then
// each turn where the turn's left operand ends, the last one closed by an
// exit. Bases and turns rank as a walk from the loop's head meets them, each
// rule's arms in the order written.

#include "cursiv/engines/lr/gen/lr_build_elect.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <numeric>
#include <utility>
#include <vector>

#include "cursiv/engines/lr/gen/LrBuildCfg.hpp"
#include "cursiv/engines/lr/gen/LrGuard.hpp"
#include "cursiv/engines/lr/gen/lr_build_passes.hpp"
#include "cursiv/engines/lr/table/LrColdTables.hpp"
#include "cursiv/engines/shared/table/Symbol.hpp"
#include "cursiv/engines/shared/table/WriteKind.hpp"

namespace cursiv {
namespace {

constexpr std::uint32_t kNoWord = ~std::uint32_t{0};

std::vector<unsigned> productions_by_rule(const LrBuildCfg& g) {
  std::vector<unsigned> order(g.nprods);
  std::iota(order.begin(), order.end(), 0u);
  std::ranges::stable_sort(order, [&](unsigned a, unsigned b) {
    return std::pair(g.prods[a].lhs, g.prodBuild[a].priorityAlt) <
           std::pair(g.prods[b].lhs, g.prodBuild[b].priorityAlt);
  });
  return order;
}

}  // namespace

void lr_state_symbols(LrBuildTable& A) {
  const LrBuildCfg& g = A.cfg;
  // Each way into a state, `f(from, to, symbol)`: a shift, a goto, or one
  // landing of a guard's.
  const auto each_entry = [&](auto&& f) {
    for (unsigned s = 0; s < A.nstates; ++s) {
      for (const LrAction& action : A.action_row(s))
        A.each_landing(action, [&](unsigned to) { f(s, to, action.term); });
      for (const auto& e : A.gotoRows[s]) {
        const auto x = static_cast<std::uint16_t>(g.nterms + e.nt);
        const auto land = [&](unsigned to) { f(s, to, x); };
        if (e.guard == 0)
          land(e.to);
        else
          A.guards[e.guard - 1u].each_landing(land);
      }
    }
  };
  std::vector<std::uint16_t> sym(A.nstates, kLrNoStateSym);
  each_entry([&](unsigned, unsigned to, std::uint16_t x) { sym[to] = x; });
  std::vector<std::uint16_t> below(A.nstates, kLrNoStateSym);
  std::vector<std::uint8_t> seen(A.nstates, 0);
  each_entry([&](unsigned from, unsigned to, std::uint16_t) {
    if (!seen[to]) {
      seen[to] = 1;
      below[to] = sym[from];
    } else if (below[to] != sym[from]) {
      below[to] = kLrNoStateSym;
    }
  });
  for (unsigned s = 0; s < A.nstates; ++s) {
    A.stateSym[s] = sym[s];
    A.stateBelow[s] = below[s];
  }
}

void lr_elect_words(LrBuildTable& A) {
  const LrBuildCfg& g = A.cfg;
  const unsigned nP = g.nprods, nN = g.nnts;
  const auto lead = [&](unsigned p) -> unsigned {
    const unsigned b = g.nt_after(p, 0);
    return b == kNoRow ? nN : b;
  };
  std::vector<std::uint8_t> turn(nP, 0);
  for (unsigned p = 0; p < nP; ++p) turn[p] = g.turns(p);
  const std::vector<unsigned> order = productions_by_rule(g);
  std::vector<unsigned> from(nN + 1u, 0);
  for (unsigned p = 0; p < nP; ++p) ++from[g.prods[p].lhs + 1u];
  for (unsigned n = 0; n < nN; ++n) from[n + 1] += from[n];
  const auto alt = [&](unsigned i) {
    return g.prodBuild[order[i]].priorityAlt;
  };

  std::vector<std::uint32_t> word(nP, kNoWord);
  // Outside a recursion an arm ranks among its rule's, where there are two.
  for (unsigned n = 0; n < nN; ++n) {
    if (g.loop_of(n) != kNoRow || from[n] == from[n + 1]) continue;
    std::uint32_t rank = 0;
    for (unsigned i = from[n]; i < from[n + 1]; ++i) {
      if (i != from[n] && alt(i) != alt(i - 1)) ++rank;
      word[order[i]] = rank;
    }
    if (rank == 0)
      for (unsigned i = from[n]; i < from[n + 1]; ++i) word[order[i]] = kNoWord;
  }
  // Inside one, bases and turns are numbered as the walk meets them; the
  // subsets of one arm share its number.
  std::vector<std::uint8_t> outside(nN, 0), seen(nN, 0);
  for (unsigned p = 0; p < nP; ++p) {
    const LrLowProd& q = g.prods[p];
    for (unsigned i = turn[p] ? 1u : 0u; i < q.len; ++i)
      if (const std::uint16_t s = g.sym_at(q, i); !is_term(s))
        outside[nt_index(s)] = 1;
  }
  // The entry is the rule called from outside the loop; a climb's loop, which
  // no copy splits, has several and keeps the first.
  std::vector<unsigned> entry(nN, nN);
  for (unsigned n = 0; n < nN; ++n)
    if (const auto l = g.loop_of(n); l != kNoRow) {
      unsigned& e = entry[l];
      if (e == nN || (outside[n] && !outside[e])) e = n;
    }
  std::uint32_t bases = 0, turns = 0;
  std::vector<std::pair<unsigned, unsigned>> walk;
  const auto visit = [&](unsigned root) {
    seen[root] = 1;
    walk.assign(1, {root, from[root]});
    while (!walk.empty()) {
      const unsigned n = walk.back().first;
      const unsigned i = walk.back().second++;
      if (i == from[n + 1]) {
        walk.pop_back();
        continue;
      }
      const unsigned p = order[i];
      const bool enters = turn[p] && !seen[lead(p)];
      std::uint32_t& w = word[p];
      bool shared = false;
      for (unsigned j = i; j-- > from[n] && alt(j) == alt(i) && !shared;)
        if (turn[order[j]] == turn[p]) {
          w = word[order[j]];
          shared = true;
        }
      if (!shared && w == kNoWord && !enters) w = turn[p] ? turns++ : bases++;
      if (enters) {
        seen[lead(p)] = 1;
        walk.emplace_back(lead(p), from[lead(p)]);
      }
    }
  };
  for (unsigned n = 0; n < nN; ++n)
    if (g.loop_of(n) != kNoRow && entry[g.loop_of(n)] == n && !seen[n])
      visit(n);
  for (unsigned n = 0; n < nN; ++n)
    if (g.loop_of(n) != kNoRow && !seen[n]) visit(n);

  // At one position: the exit of the loop the child before it closes, the
  // arm, then the options and turns the arm decides there.
  struct Laid {
    std::uint32_t pos, kind, word;
  };
  std::vector<Laid> laid;
  A.electAt.assign(nP + 1u, 0);
  A.electWord.clear();
  A.electPos.clear();
  for (unsigned p = 0; p < nP; ++p) {
    A.electAt[p] = static_cast<std::uint32_t>(A.electWord.size());
    const LrLowProd& q = g.prods[p];
    const LrProdBuild& b = g.prodBuild[p];
    const std::uint32_t armAt = turn[p] ? 1u : 0u;
    laid.clear();
    if (word[p] != kNoWord) laid.push_back({armAt, 1, word[p]});
    for (std::uint32_t d = b.decideAt; d < b.decideAt + b.decideLen; ++d)
      laid.push_back(
          {std::max(g.decisions[d] >> 16, armAt), 2, g.decisions[d] & 0xffffu});
    for (unsigned i = turn[p] ? 1u : 0u; i < q.len; ++i)
      if (const std::uint16_t s = g.sym_at(q, i);
          !is_term(s) && g.loop_of(nt_index(s)) != kNoRow)
        laid.push_back({i + 1u, 0, kLrExitWord});
    std::ranges::stable_sort(laid, [](const Laid& x, const Laid& y) {
      return std::pair(x.pos, x.kind) < std::pair(y.pos, y.kind);
    });
    for (const Laid& x : laid) {
      A.electWord.push_back(x.word);
      A.electPos.push_back(static_cast<std::uint16_t>(x.pos));
    }
  }
  A.electAt[nP] = static_cast<std::uint32_t>(A.electWord.size());
}

void lr_elect_runs(LrBuildTable& A) {
  const LrBuildCfg& g = A.cfg;
  const unsigned nP = g.nprods, nN = g.nnts;
  const std::vector<unsigned> order = productions_by_rule(g);

  // A pass-through the table skips leaves its callee where its caller stood:
  // the decisions of the skipped run are restored around the callee, those at
  // its head before it, those at its tail after.
  A.electWatchAt.assign(nP + 1u, 0);
  A.electWatch.clear();
  A.electChainKey.clear();
  A.electChainAt.assign(1, 0);
  A.electChainWord.clear();
  A.electChainPos.clear();
  const bool symbolsFit = std::size_t{g.nterms} + nN <= 0xFFFFu;
  std::vector<std::vector<unsigned>> below(symbolsFit ? nN : 0);
  bool unitWords = false;
  if (symbolsFit)
    for (const unsigned p : order)
      if (lr_unit_passes(g, p)) {
        below[g.prods[p].lhs].push_back(p);
        unitWords = unitWords || A.electAt[p] != A.electAt[p + 1u];
      }
  // A path names only the productions that spell decisions. Plain aliases
  // share that link unchanged, instead of copying an ever longer prefix.
  constexpr std::size_t noPath = ~std::size_t{0};
  struct Path {
    unsigned prod;
    std::size_t previous;
  };
  struct Run {
    std::uint32_t key;
    std::size_t path;
  };
  std::vector<Path> paths;
  std::vector<Run> runs;
  std::vector<std::uint8_t> carries(nN, 0);
  const auto collect = [&] {
    std::vector<std::uint8_t> reached(std::size_t{g.nterms} + nN);
    std::vector<std::pair<unsigned, std::size_t>> walk;
    for (unsigned x = 0; x < nN; ++x) {
      std::ranges::fill(reached, 0);
      walk.assign(1, {x, noPath});
      while (!walk.empty()) {
        const auto [at, path] = walk.back();
        walk.pop_back();
        for (const unsigned p : below[at]) {
          const std::uint16_t s = g.sym_at(g.prods[p], 0);
          const unsigned y = is_term(s) ? s : g.nterms + nt_index(s);
          if (reached[y]) continue;
          reached[y] = 1;
          std::size_t run = path;
          if (A.electAt[p] != A.electAt[p + 1u]) {
            run = paths.size();
            paths.push_back({p, path});
          }
          if (run != noPath) carries[x] = 1;
          runs.push_back({x << 16 | y, run});
          if (!is_term(s)) walk.emplace_back(nt_index(s), run);
        }
      }
    }
  };
  if (unitWords) {
    collect();
    for (unsigned p = 0; p < nP; ++p) {
      A.electWatchAt[p] = static_cast<std::uint32_t>(A.electWatch.size());
      const LrLowProd& q = g.prods[p];
      g.each_nt(q, [&](unsigned i, std::uint16_t nt) {
        if (carries[nt]) A.electWatch.push_back(i << 16 | nt);
      });
    }
    A.electWatchAt[nP] = static_cast<std::uint32_t>(A.electWatch.size());
  }

  // States that read one reduction alike fuse, whatever their productions
  // decided: each production names those it may stand for, and their
  // symbols, so the one the children read is found again.
  A.electTwinAt.assign(nP + 1u, 0);
  A.electTwin.clear();
  A.electRhsAt.assign(nP + 1u, 0);
  A.electRhs.clear();
  if (symbolsFit) {
    std::map<std::vector<std::uint32_t>, std::uint32_t> classOf;
    std::vector<std::uint32_t> key, cls(nP);
    std::vector<std::vector<unsigned>> members;
    for (unsigned p = 0; p < nP; ++p) {
      lr_reduce_key(g, p, key);
      const auto [at, fresh] =
          classOf.try_emplace(key, static_cast<std::uint32_t>(members.size()));
      if (fresh) members.emplace_back();
      cls[p] = at->second;
      members[at->second].push_back(p);
    }
    // Two productions an election cannot tell apart need not be found again.
    const auto read = [&](unsigned p) {
      std::vector<std::uint32_t> out;
      for (std::uint32_t k = A.electAt[p]; k < A.electAt[p + 1]; ++k)
        out.insert(out.end(), {A.electWord[k], A.electPos[k]});
      out.push_back(kNoWord);
      if (!A.electWatchAt.empty())
        out.insert(out.end(), A.electWatch.begin() + A.electWatchAt[p],
                   A.electWatch.begin() + A.electWatchAt[p + 1]);
      return out;
    };
    std::vector<std::uint8_t> uniform(members.size());
    for (std::size_t c = 0; c < members.size(); ++c) {
      const auto first = read(members[c].front());
      uniform[c] = std::ranges::all_of(
          members[c], [&](unsigned q) { return read(q) == first; });
    }
    for (unsigned p = 0; p < nP; ++p) {
      A.electTwinAt[p] = static_cast<std::uint32_t>(A.electTwin.size());
      A.electRhsAt[p] = static_cast<std::uint32_t>(A.electRhs.size());
      if (uniform[cls[p]]) continue;
      const auto& group = members[cls[p]];
      for (const unsigned q : group)
        A.electTwin.push_back(static_cast<std::uint16_t>(q));
      const LrLowProd& q = g.prods[p];
      for (unsigned i = 0; i < q.len; ++i) {
        const std::uint16_t s = g.sym_at(q, i);
        A.electRhs.push_back(static_cast<std::uint16_t>(
            is_term(s) ? s : g.nterms + nt_index(s)));
      }
    }
    A.electTwinAt[nP] = static_cast<std::uint32_t>(A.electTwin.size());
    A.electRhsAt[nP] = static_cast<std::uint32_t>(A.electRhs.size());
  }
  if (A.electTwin.empty()) {
    A.electTwinAt.clear();
    A.electRhsAt.clear();
    A.electRhs.clear();
  }
  if (A.electWatch.empty()) A.electWatchAt.clear();
  if (A.electWatch.empty() && A.electTwin.empty()) {
    A.electChainAt.clear();
  } else {
    // Wordless aliases need their relation only if twins use it. In the
    // common case with neither watches nor twins, no paths are made at all.
    if (!unitWords) collect();
    std::ranges::sort(runs, {}, &Run::key);
    std::vector<unsigned> decisions;
    const auto words_at = [&](unsigned p, std::uint16_t pos) {
      for (std::uint32_t k = A.electAt[p]; k < A.electAt[p + 1u]; ++k)
        if (A.electPos[k] == pos) {
          A.electChainWord.push_back(A.electWord[k]);
          A.electChainPos.push_back(pos);
        }
    };
    for (const Run& run : runs) {
      A.electChainKey.push_back(run.key);
      decisions.clear();
      for (auto path = run.path; path != noPath; path = paths[path].previous)
        decisions.push_back(paths[path].prod);
      for (auto p = decisions.rbegin(); p != decisions.rend(); ++p)
        words_at(*p, 0);
      for (const unsigned p : decisions) words_at(p, 1);
      A.electChainAt.push_back(
          static_cast<std::uint32_t>(A.electChainWord.size()));
    }
  }
  if (A.electWord.empty()) A.electAt.clear();
}

}  // namespace cursiv
