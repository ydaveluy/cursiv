// Conditions leave the right-hand sides: each one guards the position it
// stands before, or the reduction when nothing follows it.
#include "cursiv/engines/lr/gen/lr_build_guards.hpp"

#include <cstdint>
#include <span>
#include <vector>

#include "cursiv/engines/lr/gen/LrLowProd.hpp"
#include "cursiv/engines/lr/gen/LrLowSlot.hpp"
#include "cursiv/engines/shared/table/Symbol.hpp"
#include "cursiv/engines/shared/table/WriteKind.hpp"

namespace cursiv {
namespace {

// A new condition: `a`, and `b` where not zero, read tight where `tight`
// says. Zero when no id is left.
std::uint8_t lr_guard_lay(LrBuildCfg& g, std::uint8_t a, std::uint8_t b,
                          bool tight) {
  const std::uint16_t two[] = {static_cast<std::uint16_t>(a - 1u),
                               static_cast<std::uint16_t>(b - 1u)};
  const std::uint16_t k =
      g.gap_conjoin(std::span(two, b != 0 ? 2u : 1u));
  if (k == kNoRow) return 0;
  g.gapTight[k] = tight ? 1u : 0u;
  return static_cast<std::uint8_t>(k + 1u);
}

}  // namespace

bool lr_guard_positions(LrBuildCfg& g, ChainPool& writes) {
  const unsigned nN = g.nnts;
  const unsigned nP = g.nprods;
  g.gapTight.row(0, g.ngapConds);
  std::vector<unsigned> count(nN, 0u);
  for (unsigned p = 0; p < nP; ++p) ++count[g.prods[p].lhs];
  // The rules that are a condition: one empty, gated production. A record
  // that states nothing only gave a directive a span to ride.
  std::vector<std::uint8_t> cond(nN, std::uint8_t{});
  for (unsigned p = 0; p < nP; ++p) {
    const LrLowProd& q = g.prods[p];
    const bool bare = q.node == kNoRow || writes.chains[q.node].count == 0;
    if (q.len == 0 && q.gap != 0 && bare && count[q.lhs] == 1)
      cond[q.lhs] = q.gap;
  }
  const auto cond_of = [&](std::uint16_t s) -> std::uint8_t {
    return is_term(s) ? 0 : cond[nt_index(s)];
  };
  // One id per conjunction and per condition read tight.
  std::vector<std::vector<std::uint8_t>> joined(g.ngapConds + 1u);
  std::vector<std::uint8_t> tightOf(g.ngapConds + 1u, std::uint8_t{});
  bool ok = true;
  const auto join = [&](std::uint8_t a, std::uint8_t b) -> std::uint8_t {
    if (a == 0 || a == b) return b;
    if (b == 0) return a;
    if (joined[a].size() <= b) joined[a].resize(b + 1u, 0);
    if (joined[a][b] == 0) {
      const std::uint8_t id = lr_guard_lay(g, a, b, false);
      joined.resize(g.ngapConds + 1u);
      tightOf.resize(g.ngapConds + 1u, 0);
      joined[a][b] = id;
    }
    ok = ok && joined[a][b] != 0;
    return joined[a][b];
  };
  const auto tight_twin = [&](std::uint8_t k) -> std::uint8_t {
    if (k == 0 || g.gapTight[k - 1u] != 0) return k;
    if (tightOf[k] == 0) {
      const std::uint8_t id = lr_guard_lay(g, k, 0, true);
      joined.resize(g.ngapConds + 1u);
      tightOf.resize(g.ngapConds + 1u, 0);
      tightOf[k] = id;
    }
    ok = ok && tightOf[k] != 0;
    return tightOf[k];
  };

  // A run of conditions and the position after it stand on one boundary:
  // where a region edge parts them, the one naming no trivia yields.
  const auto bare = [&](std::uint8_t k) {
    return k < g.cfgBare.size() && g.cfgBare[k] != 0;
  };
  const auto edge = [&](std::uint8_t before, std::uint8_t after) {
    return bare(after) && !bare(before) ? before : after;
  };
  std::vector<LrLowSlot> slots;
  std::vector<std::uint16_t> sym;
  std::vector<std::uint8_t> cfgs, guard;
  const auto push = [&](LrLowSlot s, std::uint16_t x, std::uint8_t c,
                        std::uint8_t k) {
    slots.push_back(s);
    sym.push_back(x);
    cfgs.push_back(c);
    guard.push_back(k);
  };
  // Where each old position lands: a condition on the one it guards.
  std::vector<std::uint32_t> moved;
  for (unsigned p = 0; p < nP; ++p) {
    LrLowProd& q = g.prods[p];
    LrProdBuild& b = g.prodBuild[p];
    const auto at = static_cast<std::uint32_t>(sym.size());
    const auto self = static_cast<std::uint16_t>(p);
    moved.assign(q.len + 1u, 0);
    std::uint32_t settled = 0;
    const auto land = [&](unsigned upTo) {
      for (; settled < upTo; ++settled)
        moved[settled] = static_cast<std::uint32_t>(sym.size()) - at;
    };
    for (unsigned i = 0; i < q.len;) {
      if (cond_of(g.sym_at(q, i)) == 0) {
        land(i + 1u);
        push(g.slots[q.at + i], g.sym_at(q, i), g.cfg_at(self, i), 0);
        ++i;
        continue;
      }
      const unsigned first = i;
      std::uint8_t k = 0;
      std::uint16_t row = kNoRow;
      for (; i < q.len && cond_of(g.sym_at(q, i)) != 0; ++i) {
        k = join(k, cond_of(g.sym_at(q, i)));
        row = writes.concat(row, g.row_at(q, i));
      }
      // A region naming no trivia the next position leaves keeps that
      // position's boundary, and the condition reads the region's own bytes.
      const std::uint8_t before = g.cfg_at(self, first);
      const bool twin = bare(before);
      if (i < q.len) {
        const std::uint8_t after = g.cfg_at(self, i);
        land(i + 1u);
        push(LrLowSlot{writes.concat(row, g.row_at(q, i))}, g.sym_at(q, i),
             edge(before, after), twin && before != after ? tight_twin(k) : k);
        ++i;
      } else if (row != kNoRow) {
        // Nothing follows to carry the run's writes: it stays a reduction.
        for (unsigned j = first; j < q.len; ++j) {
          land(j + 1u);
          push(g.slots[q.at + j], g.sym_at(q, j), g.cfg_at(self, j), 0);
        }
      } else {
        const std::uint8_t after = b.tailCfg;
        q.gap = twin && before != after ? tight_twin(k) : k;
        b.tailCfg = edge(before, after);
      }
    }
    land(q.len + 1u);
    for (std::uint32_t d = b.decideAt; d < b.decideAt + b.decideLen; ++d) {
      std::uint32_t& x = g.decisions[d];
      x = moved[x >> 16] << 16 | (x & 0xffffu);
    }
    q.at = at;
    q.len = static_cast<std::uint16_t>(sym.size() - at);
  }
  // A condition no position names waits on nothing: its empty rule must not
  // flag the grammar as one that gates reductions.
  std::vector<std::uint8_t> named(nN, std::uint8_t{});
  for (const std::uint16_t s : sym)
    if (!is_term(s)) named[nt_index(s)] = 1;
  for (unsigned p = 0; p < nP; ++p) {
    LrLowProd& q = g.prods[p];
    if (cond[q.lhs] != 0 && q.len == 0 && named[q.lhs] == 0) q.gap = 0;
  }
  g.nslots = static_cast<std::uint32_t>(sym.size());
  g.slots.v = std::move(slots);
  g.slotSym.v = std::move(sym);
  g.slotCfg.v = std::move(cfgs);
  g.slotGuard.v = std::move(guard);
  return ok && !writes.capped;
}

}  // namespace cursiv
