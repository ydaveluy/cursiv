// The conditions that stand at one token: those at the head of a position,
// through its calls and past what reads nothing, and those right in front of
// it. They share the token's lexical choice.
#pragma once

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

#include "cursiv/ll/gen/LlBuiltProgram.hpp"
#include "cursiv/shared/table/DynSet.hpp"

namespace cursiv::engine {

struct LlStanding {
  // Per position, the condition ids that stand at the token it reads first.
  std::vector<DynSet> point;

  explicit LlStanding(const LlBuiltProgram& b) : point(b.cfg.nnodes) {
    const auto& g = b.cfg;
    const auto& lead = b.lead;
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
            // Behind a child that may read something a new token begins: where
            // it read nothing, it read the token, under what stands there.
            std::vector<DynSet> from(n.count + 1u);
            for (unsigned i = n.count; i-- > 0;) {
              const auto child = g.childIds[n.sym + i];
              from[i] =
                  b.nullable[child] ? lead[child] | from[i + 1] : lead[child];
            }
            const auto begin = pending.size();
            unsigned run = 0;
            for (unsigned i = 0; i < n.count; ++i) {
              const auto child = g.childIds[n.sym + i];
              pending.emplace_back(child,
                                   run == 0 ? point[visit.id] : from[run]);
              if (!b.nullable[child] || b.first[child].any()) run = i + 1;
            }
            std::reverse(pending.begin() + begin, pending.end());
            break;
          }
          case LlShape::alt:
          case LlShape::rep: {
            const auto children = g.children(n);
            for (auto child = children.rbegin(); child != children.rend();
                 ++child)
              pending.emplace_back(*child, point[visit.id]);
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
      spread(g.climbs[k].selectNode);
  }
};

}  // namespace cursiv::engine
