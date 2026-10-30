// What a rule reads before it ends, from each position of the graph: at a
// state a call returns into, what the caller reads once the call is done.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "cursiv/engines/ll/gen/LlBuiltProgram.hpp"
#include "cursiv/engines/shared/table/DynSet.hpp"

namespace cursiv {

// Per state of the graph, what its rule reads from there on before it ends,
// and whether it may end reading nothing. A condition is crossed as if it
// held, as a prediction speculates over it.
struct LlExitReads {
  std::vector<DynSet> reads;
  std::vector<std::uint8_t> ends;

  explicit LlExitReads(const LlBuiltProgram& a)
      : reads(a.nstates), ends(a.nstates, 0) {
    const auto out = [&](std::uint32_t s) {
      return std::span(a.edges.data() + a.states[s].edge, a.states[s].nedges);
    };
    // A state is read again whenever one it goes on into grows.
    std::vector<std::vector<std::uint32_t>> into(a.nstates);
    for (std::uint32_t s = 0; s < a.nstates; ++s)
      for (const LlAtnEdge& e : out(s)) {
        if (e.kind == LlEdgeKind::atom) continue;
        into[e.target].push_back(s);
        if (e.kind == LlEdgeKind::rule) into[e.follow].push_back(s);
      }
    std::vector<std::uint32_t> todo(a.nstates);
    for (std::uint32_t s = 0; s < a.nstates; ++s) todo[s] = s;
    std::vector<std::uint8_t> queued(a.nstates, 1);
    while (!todo.empty()) {
      const std::uint32_t s = todo.back();
      todo.pop_back();
      queued[s] = 0;
      bool grew = false;
      bool end = a.states[s].kind == LlAtnKind::ruleStop;
      for (const LlAtnEdge& e : out(s)) {
        if (e.kind == LlEdgeKind::atom) {
          grew = grew || !reads[s].test(e.label);
          reads[s].set(e.label);
          continue;
        }
        grew = reads[s].add(reads[e.target]) || grew;
        bool on = ends[e.target] != 0;
        // A call reads what follows it too where its callee may read nothing.
        if (e.kind == LlEdgeKind::rule && on) {
          grew = reads[s].add(reads[e.follow]) || grew;
          on = ends[e.follow] != 0;
        }
        end = end || on;
      }
      if (end && ends[s] == 0) {
        ends[s] = 1;
        grew = true;
      }
      if (grew)
        for (const std::uint32_t from : into[s])
          if (queued[from] == 0) {
            queued[from] = 1;
            todo.push_back(from);
          }
    }
  }
};

}  // namespace cursiv
