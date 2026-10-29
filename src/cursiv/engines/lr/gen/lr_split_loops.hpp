// A left recursion is read as the loop of the rule a call starts with, its
// arms ranked as a walk from that rule meets them. The walk is unfolded into
// rules of its own for each head, and for each way it meets a rule, so every
// arm has one rank. A climb's loop is numbered by rule, from one head.
#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <vector>

#include "cursiv/engines/lr/gen/LrBuckets.hpp"
#include "cursiv/engines/lr/gen/LrLowered.hpp"
#include "cursiv/engines/lr/gen/lr_copy_prod.hpp"
#include "cursiv/engines/lr/gen/lr_lower_define.hpp"
#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"
#include "cursiv/engines/shared/table/Symbol.hpp"

namespace cursiv {

inline void lr_split_loops(LrLowered& o) {
  LrBuildCfg& c = o.cfg;
  const unsigned nN = c.nnts, nP = c.nprods;
  std::vector<unsigned> lead(nP, nN);
  const auto by_lhs = lr_by_lhs(c);
  for (unsigned p = 0; p < nP; ++p)
    if (c.turns(p)) lead[p] = nt_index(c.sym_at(c.prods[p], 0));

  const auto live = lr_reachable(c, by_lhs);
  std::vector<std::uint8_t> head(nN, 0);
  for (unsigned p = 0; p < nP; ++p) {
    const LrLowProd& q = c.prods[p];
    if (!live[q.lhs]) continue;
    for (unsigned i = lead[p] == nN ? 0u : 1u; i < q.len; ++i)
      if (const std::uint16_t s = c.sym_at(q, i);
          !is_term(s) && c.loop_of(nt_index(s)) != kNoRow)
        head[nt_index(s)] = 1;
  }
  std::map<std::uint16_t, std::vector<std::uint16_t>> members, heads;
  std::uint16_t next = 0;
  for (unsigned n = 0; n < nN; ++n) {
    if (c.loop_of(n) == kNoRow) continue;
    members[c.loop_of(n)].push_back(static_cast<std::uint16_t>(n));
    if (head[n]) heads[c.loop_of(n)].push_back(static_cast<std::uint16_t>(n));
    next = std::max(next, static_cast<std::uint16_t>(c.loop_of(n) + 1u));
  }

  struct Place {
    std::uint16_t rule;
    int parent;
    std::uint16_t nt = kNoRow;
    std::vector<int> goes;
  };
  for (const auto& [l, hs] : heads) {
    std::vector<Place> places;
    std::vector<std::uint16_t> loops;
    // One active place per rule on the DFS path. A sibling must see its own
    // path again, so entries leave this index when their frame is popped.
    std::vector<int> active(nN, -1);
    struct Visit {
      int place;
      std::size_t arm = 0;
    };
    std::vector<Visit> walk;
    const auto enter = [&](int at) {
      active[places[at].rule] = at;
      places[at].goes.assign(by_lhs.of(places[at].rule).size(), -1);
      walk.push_back({at});
    };
    for (std::size_t k = 0; k < hs.size(); ++k) {
      places.push_back({hs[k], -1, kNoRow, {}});
      loops.push_back(static_cast<std::uint16_t>(next + k));
      enter(static_cast<int>(places.size()) - 1);
      while (!walk.empty()) {
        const int at = walk.back().place;
        const auto arms = by_lhs.of(places[at].rule);
        if (walk.back().arm == arms.size()) {
          active[places[at].rule] = -1;
          walk.pop_back();
          continue;
        }
        const std::size_t i = walk.back().arm++;
        const unsigned x = lead[arms[i]];
        if (x == nN) continue;
        int to = active[x];
        const bool fresh = to < 0;
        if (fresh) {
          if (places.size() >= kNtBase) {
            lower_refuse(c, LowerFail::tooManyRules);
            throw LowerRefused(LowerFail::tooManyRules);
          }
          to = static_cast<int>(places.size());
          places.push_back({static_cast<std::uint16_t>(x), at, kNoRow, {}});
          loops.push_back(loops[at]);
        }
        places[at].goes[i] = to;
        if (fresh) enter(to);
      }
    }
    if (places.size() == members[l].size()) continue;
    next = static_cast<std::uint16_t>(next + hs.size());

    std::vector<std::uint8_t> kept(nN, 0);
    for (std::size_t at = 0; at < places.size(); ++at) {
      Place& place = places[at];
      const std::uint16_t rule = place.rule;
      if (place.parent < 0 || (!head[rule] && !kept[rule])) {
        place.nt = rule;
        kept[rule] = 1;
      } else {
        place.nt = lr_new_aux(c, rule);
      }
      c.leftLoop[place.nt] = loops[at];
    }
    for (const Place& place : places) {
      const auto arms = by_lhs.of(place.rule);
      for (std::size_t i = 0; i < arms.size(); ++i) {
        std::uint16_t at = arms[i];
        if (place.nt != place.rule) {
          at = lr_copy_prod(c, at, place.nt);
          if (at == kNoRow) throw LowerRefused(LowerFail::tooManyProductions);
        }
        if (place.goes[i] >= 0)
          c.slotSym[c.prods[at].at] = nt_sym(places[place.goes[i]].nt);
      }
    }
  }
}

}  // namespace cursiv
