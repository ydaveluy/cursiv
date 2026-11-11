// Conditions leave the right-hand sides: each one guards the position it
// stands before, or the reduction when nothing follows it.
#include "cursiv/lr/gen/lr_build_guards.hpp"

#include <cstdint>
#include <span>
#include <vector>

#include "cursiv/lr/gen/LrLowProd.hpp"
#include "cursiv/shared/table/Symbol.hpp"
#include "cursiv/shared/table/WriteKind.hpp"

namespace cursiv::engine {
namespace {

// A new condition: `a` and `b`. Zero when no id is left.
std::uint8_t lr_guard_add(LrBuildCfg& g, std::uint8_t a, std::uint8_t b) {
  const std::uint16_t two[] = {static_cast<std::uint16_t>(a - 1u),
                               static_cast<std::uint16_t>(b - 1u)};
  const std::uint16_t k = g.gap_conjoin(std::span(two));
  return k == kNoRow ? 0 : static_cast<std::uint8_t>(k + 1u);
}

}  // namespace

bool lr_guard_positions(LrBuildCfg& g, ChainPool& writes) {
  const unsigned nN = g.nnts;
  const unsigned nP = g.nprods;
  std::vector count(nN, 0u);
  for (unsigned p = 0; p < nP; ++p) ++count[g.prods[p].lhs];
  // The rules that are a condition: one empty, gated production that asserts
  // no end. A record that states nothing only gave a directive a span to attach
  // to.
  std::vector cond(nN, std::uint8_t{});
  for (unsigned p = 0; p < nP; ++p) {
    const LrLowProd& q = g.prods[p];
    const bool bare = q.node == kNoRow || writes.chains[q.node].count == 0;
    if (q.len == 0 && q.gap != 0 && bare && count[q.lhs] == 1 && !q.endOnly)
      cond[q.lhs] = q.gap;
  }
  const auto cond_of = [&](std::uint16_t s) -> std::uint8_t {
    return is_term(s) ? 0 : cond[nt_index(s)];
  };
  // One id per conjunction.
  std::vector<std::vector<std::uint8_t>> joined(g.ngapConds + 1u);
  bool ok = true;
  const auto join = [&](std::uint8_t a, std::uint8_t b) {
    if (a == 0 || a == b) return b;
    if (joined[a].size() <= b) joined[a].resize(b + 1u, 0);
    if (joined[a][b] == 0) {
      const std::uint8_t id = lr_guard_add(g, a, b);
      joined.resize(g.ngapConds + 1u);
      joined[a][b] = id;
    }
    ok = ok && joined[a][b] != 0;
    return joined[a][b];
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
  // Where each old position goes: a condition on the one it guards.
  std::vector<std::uint32_t> moved;
  for (unsigned p = 0; p < nP; ++p) {
    LrLowProd& q = g.prods[p];
    const auto at = static_cast<std::uint32_t>(slots.size());
    const auto self = static_cast<std::uint16_t>(p);
    moved.assign(q.len + 1u, 0);
    std::uint32_t mapped = 0;
    const auto mark_moved = [&](unsigned upTo) {
      for (; mapped < upTo; ++mapped)
        moved[mapped] = static_cast<std::uint32_t>(slots.size()) - at;
    };
    for (unsigned i = 0; i < q.len;) {
      if (cond_of(g.sym_at(q, i)) == 0) {
        mark_moved(i + 1u);
        slots.push_back({g.row_at(q, i), g.sym_at(q, i), g.cfg_at(self, i), 0});
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
      // position's boundary, and the condition reads it too: the trivia past
      // a region is the caller's.
      const std::uint8_t before = g.cfg_at(self, first);
      if (i < q.len) {
        mark_moved(i + 1u);
        slots.push_back({writes.concat(row, g.row_at(q, i)), g.sym_at(q, i),
                         edge(before, g.cfg_at(self, i)), k});
        ++i;
      } else if (row != kNoRow) {
        // Nothing follows to carry the run's writes: it stays a reduction,
        // on the boundary past it.
        const std::uint8_t past = edge(before, q.tailCfg);
        for (unsigned j = first; j < q.len; ++j) {
          mark_moved(j + 1u);
          slots.push_back({g.row_at(q, j), g.sym_at(q, j), past, 0});
        }
      } else {
        q.gap = join(q.gap, k);
        q.tailCfg = edge(before, q.tailCfg);
      }
    }
    mark_moved(q.len + 1u);
    for (std::uint32_t d = q.decideAt; d < q.decideAt + q.decideLen; ++d) {
      std::uint32_t& x = g.decisions[d];
      x = moved[x >> 16] << 16 | (x & 0xffffu);
    }
    q.at = at;
    q.len = static_cast<std::uint16_t>(slots.size() - at);
  }
  // A condition no position names waits on nothing: its empty rule must not
  // flag the grammar as one that gates reductions.
  std::vector named(nN, std::uint8_t{});
  for (const LrLowSlot& s : slots)
    if (!is_term(s.sym)) named[nt_index(s.sym)] = 1;
  for (unsigned p = 0; p < nP; ++p) {
    LrLowProd& q = g.prods[p];
    if (cond[q.lhs] != 0 && q.len == 0 && named[q.lhs] == 0) q.gap = 0;
  }
  g.nslots = static_cast<std::uint32_t>(slots.size());
  g.slots.v = std::move(slots);
  return ok && !writes.capped;
}

}  // namespace cursiv::engine
