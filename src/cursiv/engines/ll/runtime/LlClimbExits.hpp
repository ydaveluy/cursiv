// Where a climb may end, at its exact call site: two calls made by the same
// rule need not have the same followers. Contexts intern the actual descent,
// so their continuations are computed once and retained across its tokens.
#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "cursiv/engines/ll/runtime/LlContext.hpp"
#include "cursiv/engines/ll/runtime/ll_lookahead.hpp"
#include "cursiv/engines/ll/runtime/ll_section.hpp"
#include "cursiv/engines/ll/table/LlSimulation.hpp"
#include "cursiv/engines/ll/table/LlTable.hpp"

namespace cursiv {

template <const auto& T>
struct LlClimbExits {
  struct Row {
    // The closure outran its budget: no token is refused.
    bool any;
    LlMask<T.termWords> set;
  };
  LlSimulation sim{T.atn, 8192};
  std::unordered_map<std::uint32_t, Row> rows;

  const Row& row(const LlContext& c) {
    // Most continuations read a delimiter immediately. Building a whole
    // descent for that query is quadratic on a deeply nested document.
    for (std::size_t frames = 1;; frames *= 2) {
      const auto life = sim.contexts().life();
      sim.resume();
      if (sim.contexts().life() != life) rows.clear();
      const bool whole = frames >= c.stack.size();
      sim.truncated(!whole);
      std::uint32_t ctx = kLlCtxNone;
      if (whole && c.document) {
        const LlAtnState& document = T.atn.state(T.atnStart);
        ctx = sim.push_return(ctx, T.atn.edge(document.edge).follow);
      }
      for (std::size_t i = whole ? 0 : c.stack.size() - frames;
           i < c.stack.size(); ++i)
        ctx = sim.push_return(ctx, c.stack[i]);
      if (const auto at = rows.find(ctx); at != rows.end()) return at->second;
      Row answer{};
      if (ctx == kLlCtxNone) {
        answer.any = true;
      } else {
        const std::uint32_t ret = sim.contexts().ret(ctx, 0);
        const std::uint32_t parent = sim.contexts().parent(ctx, 0);
        sim.seed(ret, parent);
        sim.possible_terms([&](std::uint16_t term) { answer.set.set(term); });
        answer.any = sim.capped();
        if (!whole && !answer.any && sim.unbound()) continue;
      }
      return rows.emplace(ctx, answer).first->second;
    }
  }
};

// Whether this caller can continue at the climb's end.
template <const auto& T>
inline bool ll_climb_exits(const LlContext& c, std::uint16_t sym) {
  if (ll_ends<T>(c, sym)) return true;
  static thread_local LlClimbExits<T> exits;
  const auto& row = exits.row(c);
  if (row.any || (sym < T.nterms && row.set.test(sym))) return true;
  if (!c.laValid || sym != c.la.sym) return false;
  // Another caller's longer follower may cover this caller's closer.
  LlMask<T.termWords> viable = row.set;
  thread_local std::vector<std::uint16_t> read;
  read.clear();
  return ll_match_in<T>(c.src, c.len, c.la.begin, viable, read) > c.la.begin;
}

}  // namespace cursiv
