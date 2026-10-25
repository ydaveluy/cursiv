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
#include "cursiv/engines/lr/table/LrActionKind.hpp"
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
  std::vector<std::uint16_t> sym(A.nstates, kLrNoStateSym);
  for (unsigned s = 0; s < A.nstates; ++s)
    for (const LrAction& action : A.action_row(s))
      if (action.kind == LrActionKind::shift) sym[action.arg] = action.term;
  for (unsigned s = 0; s < A.nstates; ++s)
    for (const auto& e : A.gotoRows[s])
      if (e.guard == 0) sym[e.to] = static_cast<std::uint16_t>(g.nterms + e.nt);
  std::vector<std::uint16_t> below(A.nstates, kLrNoStateSym);
  std::vector<std::uint8_t> seen(A.nstates, 0);
  const auto meet = [&](unsigned from, unsigned to) {
    if (to == LrGuard::kNoState) return;
    if (!seen[to]) {
      seen[to] = 1;
      below[to] = sym[from];
    } else if (below[to] != sym[from]) {
      below[to] = kLrNoStateSym;
    }
  };
  for (unsigned s = 0; s < A.nstates; ++s)
    for (const LrAction& action : A.action_row(s)) {
      if (action.kind == LrActionKind::shift) meet(s, action.arg);
      if (action.kind == LrActionKind::guard)
        for (const std::uint16_t to : A.guards[action.arg].to)
          meet(s, to);
    }
  for (unsigned s = 0; s < A.nstates; ++s)
    for (const auto& e : A.gotoRows[s]) {
      if (e.guard == 0) {
        meet(s, e.to);
        continue;
      }
      for (const std::uint16_t to : A.guards[e.guard - 1u].to) meet(s, to);
    }
  for (unsigned s = 0; s < A.nstates; ++s) {
    A.stateSym[s] = sym[s];
    A.stateBelow[s] = below[s];
  }
}

void lr_elect_words(LrBuildTable& A) {
  const LrBuildCfg& g = A.cfg;
  const unsigned nP = g.nprods, nN = g.nnts;
  const auto loop = [&](unsigned nt) -> std::uint16_t {
    return nt < g.leftLoop.size() ? g.leftLoop[nt] : std::uint16_t{kNoRow};
  };
  const auto lead = [&](unsigned p) -> unsigned {
    const LrLowProd& q = g.prods[p];
    if (q.len == 0 || is_term(g.sym_at(q, 0))) return nN;
    return nt_index(g.sym_at(q, 0));
  };
  // A turn: the production goes on from a left operand of its own recursion.
  std::vector<std::uint8_t> turn(nP, 0);
  for (unsigned p = 0; p < nP; ++p) {
    const auto l = loop(g.prods[p].lhs);
    turn[p] = lead(p) != nN && l != kNoRow && loop(lead(p)) == l;
  }
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
    if (loop(n) != kNoRow || from[n] == from[n + 1]) continue;
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
    if (const auto l = loop(n); l != kNoRow) {
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
    if (loop(n) != kNoRow && entry[loop(n)] == n && !seen[n]) visit(n);
  for (unsigned n = 0; n < nN; ++n)
    if (loop(n) != kNoRow && !seen[n]) visit(n);

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
          !is_term(s) && loop(nt_index(s)) != kNoRow)
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
  if (std::size_t{g.nterms} + nN <= 0xFFFFu) {
    std::vector<std::vector<unsigned>> below(nN);
    for (unsigned i = 0; i < nP; ++i) {
      const unsigned p = order[i];
      if (lr_unit_passes(g, p)) below[g.prods[p].lhs].push_back(p);
    }
    const auto words_at = [&](unsigned p, std::uint16_t pos, auto&& take) {
      for (std::uint32_t k = A.electAt[p]; k < A.electAt[p + 1]; ++k)
        if (A.electPos[k] == pos) take(A.electWord[k]);
    };
    std::vector<std::uint8_t> carries(nN, 0);
    std::vector<std::pair<std::uint32_t, std::vector<unsigned>>> runs;
    std::vector<std::uint8_t> reached;
    std::vector<std::pair<unsigned, std::vector<unsigned>>> walk;
    for (unsigned x = 0; x < nN; ++x) {
      reached.assign(std::size_t{g.nterms} + nN, 0);
      walk.assign(1, {x, {}});
      while (!walk.empty()) {
        auto [at, path] = std::move(walk.back());
        walk.pop_back();
        for (const unsigned p : below[at]) {
          const std::uint16_t s = g.sym_at(g.prods[p], 0);
          const unsigned y = is_term(s) ? s : g.nterms + nt_index(s);
          if (reached[y]) continue;
          reached[y] = 1;
          std::vector<unsigned> run = path;
          run.push_back(p);
          for (const unsigned q : run)
            if (A.electAt[q] != A.electAt[q + 1]) carries[x] = 1;
          runs.emplace_back(x << 16 | y, run);
          if (!is_term(s)) walk.emplace_back(nt_index(s), std::move(run));
        }
      }
    }
    std::ranges::sort(runs, {}, &decltype(runs)::value_type::first);
    for (const auto& [key, run] : runs) {
      A.electChainKey.push_back(key);
      for (const unsigned q : run)
        words_at(q, 0, [&](std::uint32_t w) {
          A.electChainWord.push_back(w);
          A.electChainPos.push_back(0);
        });
      for (auto q = run.rbegin(); q != run.rend(); ++q)
        words_at(*q, 1, [&](std::uint32_t w) {
          A.electChainWord.push_back(w);
          A.electChainPos.push_back(1);
        });
      A.electChainAt.push_back(
          static_cast<std::uint32_t>(A.electChainWord.size()));
    }
    for (unsigned p = 0; p < nP; ++p) {
      A.electWatchAt[p] = static_cast<std::uint32_t>(A.electWatch.size());
      const LrLowProd& q = g.prods[p];
      for (unsigned i = 0; i < q.len; ++i)
        if (const std::uint16_t s = g.sym_at(q, i);
            !is_term(s) && carries[nt_index(s)])
          A.electWatch.push_back(i << 16 | nt_index(s));
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
  if (std::size_t{g.nterms} + nN <= 0xFFFFu) {
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
    A.electChainKey.clear();
    A.electChainAt.clear();
    A.electChainWord.clear();
    A.electChainPos.clear();
  }
  if (A.electWord.empty()) A.electAt.clear();
}

}  // namespace cursiv
