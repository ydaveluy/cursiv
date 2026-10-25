// The graph a prediction walks, borrowed: the construction presents its own
// rows through this, a generated module the ones it froze. One simulation
// reads both, so a decision the build could not settle is answered at parse
// time by the same closure that failed to settle it.
#pragma once

#include <cstdint>
#include <span>

#include "cursiv/engines/ll/table/LlAtn.hpp"

namespace cursiv {

// Set on a return class whose future is the end of its rule.
inline constexpr std::uint32_t kLlTailReturn = 0x80000000u;

struct LlAtnView {
  const LlAtnState* states = nullptr;
  std::uint32_t nstates = 0;
  const LlAtnEdge* edges = nullptr;
  // Where each rule is entered.
  const std::uint32_t* ruleStart = nullptr;
  // Every state a call of one rule returns to, as runs indexed by the rule:
  // what a closure reaching the end of a rule continues into when the caller
  // is not known.
  const std::uint32_t* callerBase = nullptr;
  const std::uint32_t* callerAt = nullptr;
  // Per state, the first return whose epsilon runs stop at the same states:
  // two returns of one class have one future.
  const std::uint32_t* returnClass = nullptr;

  constexpr const LlAtnState& state(std::uint32_t s) const { return states[s]; }
  // An empty state borrows no storage, even in synthetic views without edges.
  constexpr std::span<const LlAtnEdge> outgoing(std::uint32_t s) const {
    const LlAtnState& q = states[s];
    return q.nedges == 0 ? std::span<const LlAtnEdge>{}
                        : std::span(edges + q.edge, q.nedges);
  }
  constexpr bool same_return(std::uint32_t a, std::uint32_t b) const {
    return returnClass != nullptr
               ? (returnClass[a] & ~kLlTailReturn) ==
                     (returnClass[b] & ~kLlTailReturn)
               : a == b;
  }
  // Whether the state has an edge of `kind`.
  constexpr bool has(std::uint32_t s, LlEdgeKind kind) const {
    const LlAtnState& q = states[s];
    for (std::uint32_t i = 0; i < q.nedges; ++i)
      if (edges[q.edge + i].kind == kind) return true;
    return false;
  }
  constexpr const LlAtnEdge& edge(std::uint32_t e) const { return edges[e]; }
};

}  // namespace cursiv
