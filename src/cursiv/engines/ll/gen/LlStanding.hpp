// The conditions that stand at one token: those at the head of a position,
// through the rules it calls and past what reads nothing, and those that stand
// before the token a position reads when what stands in front of it reads
// nothing. Conditions that stand at one token share its lexical choice.
#pragma once

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

#include "cursiv/engines/ll/gen/LlBuiltProgram.hpp"
#include "cursiv/engines/shared/table/DynSet.hpp"

namespace cursiv {

struct LlStanding {
  // Per position, the condition ids that stand at the token it reads first.
  std::vector<DynSet> point;

  explicit LlStanding(const LlBuiltProgram& b) : point(b.cfg.nnodes) {
    const auto& g = b.cfg;
    // Per position, the condition ids at its head: what `point` is spread
    // from, freed once it is.
    std::vector<DynSet> lead(g.nnodes);
    std::vector<std::uint8_t> done(g.nnodes, 0);
    const auto walk = [&](auto&& self, std::uint16_t id) {
      if (done[id] != 0) return;
      done[id] = 1;
      const LlNode& n = g.nodes[id];
      DynSet out;
      switch (n.kind) {
        case LlShape::condition:
          if (b.cut[n.sym].none()) out.set(n.sym);
          break;
        case LlShape::call:
          if (const LlRuleInfo& r = g.rules[n.sym];
              r.climb == kNoRow && r.body != kNoRow) {
            self(self, r.body);
            out = lead[r.body];
          }
          break;
        case LlShape::seq:
        case LlShape::alt:
        case LlShape::rep:
          for (const auto kid : g.children(n)) {
            self(self, kid);
            out |= lead[kid];
            if (n.kind == LlShape::seq && !b.nullable[kid]) break;
          }
          break;
        default:
          break;
      }
      lead[id] = std::move(out);
    };
    for (std::uint32_t id = 0; id < g.nnodes; ++id)
      walk(walk, static_cast<std::uint16_t>(id));

    struct Visit {
      std::uint16_t id;
      DynSet inherited;
    };
    const auto spread = [&](std::uint16_t id) {
      std::vector<Visit> pending{{id, {}}};
      while (!pending.empty()) {
        Visit visit = std::move(pending.back());
        pending.pop_back();
        point[visit.id] = visit.inherited | lead[visit.id];
        const LlNode& n = g.nodes[visit.id];
        switch (n.kind) {
          case LlShape::seq: {
            // Behind a kid that reads something a new token begins.
            std::vector<DynSet> from(n.count + 1u);
            for (unsigned i = n.count; i-- > 0;) {
              const auto kid = g.kids[n.sym + i];
              from[i] = b.nullable[kid] ? lead[kid] | from[i + 1] : lead[kid];
            }
            const auto begin = pending.size();
            unsigned run = 0;
            for (unsigned i = 0; i < n.count; ++i) {
              const auto kid = g.kids[n.sym + i];
              pending.emplace_back(kid, run == 0 ? point[visit.id] : from[run]);
              if (!b.nullable[kid]) run = i + 1;
            }
            std::reverse(pending.begin() + begin, pending.end());
            break;
          }
          case LlShape::alt:
          case LlShape::rep: {
            const auto children = g.children(n);
            for (auto kid = children.rbegin(); kid != children.rend(); ++kid)
              pending.emplace_back(*kid, point[visit.id]);
            break;
          }
          default:
            break;
        }
      }
    };
    // A rule's body, and a climb's operators: each begins at a token.
    for (std::uint16_t r = 1; r < g.nrules; ++r)
      if (g.rules[r].climb == kNoRow && g.rules[r].body != kNoRow)
        spread(g.rules[r].body);
    for (std::uint16_t k = 0; k < g.nclimbs; ++k)
      if (g.climbs[k].selectNode != kNoRow) spread(g.climbs[k].selectNode);
  }
};

}  // namespace cursiv
