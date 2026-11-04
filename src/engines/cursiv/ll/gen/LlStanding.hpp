// The conditions that stand at one token: those at the head of a position,
// through the rules it calls and past what reads nothing, and the conditions
// right in front of it. Conditions that stand at one token share its lexical
// choice.
#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "cursiv/ll/gen/LlBuiltProgram.hpp"
#include "cursiv/shared/table/DynSet.hpp"

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
    // Children before their parent, on a stack of its own: an alias chain may
    // be as long as the grammar, and the native stack is not.
    struct Walk {
      std::uint16_t id;
      std::uint32_t next = 0;
      DynSet out{};
    };
    const auto read_by = [&](const LlNode& n) -> std::span<const std::uint16_t> {
      if (n.kind == LlShape::seq || n.kind == LlShape::alt ||
          n.kind == LlShape::rep)
        return g.children(n);
      return {};
    };
    // A climb reads its operand first.
    const auto body_of = [&](const LlNode& n) {
      std::uint16_t callee = n.sym;
      while (g.rules[callee].climb != kNoRow)
        callee = g.climbs[g.rules[callee].climb].primary;
      return g.rules[callee].body;
    };
    std::vector<Walk> walks;
    const auto enter = [&](std::uint16_t id) {
      done[id] = 1;
      walks.push_back({id});
    };
    for (std::uint32_t root = 0; root < g.nnodes; ++root) {
      if (done[root] != 0) continue;
      enter(static_cast<std::uint16_t>(root));
      while (!walks.empty()) {
        const std::size_t top = walks.size() - 1;
        const std::uint16_t id = walks[top].id;
        const LlNode& n = g.nodes[id];
        if (n.kind == LlShape::call) {
          const std::uint16_t body = body_of(n);
          if (body != kNoRow && done[body] == 0) {
            enter(body);
            continue;
          }
          if (body != kNoRow) walks[top].out = lead[body];
        } else if (n.kind == LlShape::condition) {
          if (b.cut[n.sym].none()) walks[top].out.set(n.sym);
        } else {
          const std::span<const std::uint16_t> kids = read_by(n);
          bool descended = false;
          while (walks[top].next < kids.size()) {
            const std::uint16_t kid = kids[walks[top].next];
            if (done[kid] == 0) {
              enter(kid);
              descended = true;
              break;
            }
            walks[top].out |= lead[kid];
            ++walks[top].next;
            if (n.kind == LlShape::seq && !b.nullable[kid])
              walks[top].next = static_cast<std::uint32_t>(kids.size());
          }
          if (descended) continue;
        }
        lead[id] = std::move(walks[top].out);
        walks.pop_back();
      }
    }

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
            // Behind a kid that may read something a new token begins: where
            // it read nothing, it read the token, under what stands there.
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
              if (!b.nullable[kid] || b.first[kid].any()) run = i + 1;
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
      spread(g.climbs[k].selectNode);
  }
};

}  // namespace cursiv
