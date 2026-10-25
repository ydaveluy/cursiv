// One reading of an episode: where it stands in the graph, the stack it will
// return into, and the words of the derivation that brought it here.
#pragma once

#include <cstdint>

#include "cursiv/engines/ll/runtime/LlHistory.hpp"
#include "cursiv/engines/ll/table/LlSimulation.hpp"

namespace cursiv {

// No rule completed between a token and the reading that reads the next.
inline constexpr std::uint32_t kLlNoReduce = 0xFFFFFFFFu;

struct LlThread {
  std::uint32_t state = 0;
  // A node of the pool, a cell of the stack set, or the unknown caller.
  std::uint32_t stack = kLlCtxNone;
  // The decisions taken, and the lexical choices, the latter ranking first.
  LlHistory::Node arm = LlHistory::kRoot;
  LlHistory::Node lex = LlHistory::kRoot;
  // The rules completed since the last token, `kLlNoReduce` for none.
  std::uint32_t tag = kLlNoReduce;
  // The arm of the first decision this reading went by.
  std::uint16_t root = 0;
  // The state of the LR automaton this reading stands for, as the lexer asks.
  std::uint32_t kernel = 0;
};

}  // namespace cursiv
