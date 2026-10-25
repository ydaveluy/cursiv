// The program as a graph, which is the only form a prediction can walk: one
// state per position of a body, one edge per way out of it. The emitted code
// descends the program; a decision the analysis left open simulates this.
#pragma once

#include <cstdint>

#include "cursiv/engines/shared/table/WriteKind.hpp"

namespace cursiv {

// `ruleStop` is where a call returns from, so a closure that reaches it pops
// the caller's context; every other state is a position the closure walks
// through.
enum class LlAtnKind : std::uint8_t { basic, ruleStop };

struct LlAtnState {
  LlAtnKind kind = LlAtnKind::basic;
  std::uint16_t rule = kNoRow;
  // The choice leaving this state, `kNoRow` where it leaves at most one edge.
  std::uint16_t decision = kNoRow;
  std::uint32_t edge = 0, nedges = 0;

  constexpr bool operator==(const LlAtnState&) const = default;
};

// A `rule` edge consumes nothing: it enters the callee and remembers where to
// come back, which is what makes a closure's stack.
enum class LlEdgeKind : std::uint8_t { epsilon, atom, rule, condition };

struct LlAtnEdge {
  LlEdgeKind kind = LlEdgeKind::epsilon;
  // atom: the terminal | rule: the nonterminal.
  std::uint16_t label = 0;
  std::uint32_t target = 0;
  // rule: the state the call returns to. Unread for the other two.
  std::uint32_t follow = 0;

  constexpr bool operator==(const LlAtnEdge&) const = default;
};

}  // namespace cursiv
