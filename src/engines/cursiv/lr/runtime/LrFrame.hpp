// One symbol's frame on the parse stack.
#pragma once

#include <cstdint>
#include <type_traits>

#include "cursiv/grammar/ElementId.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"

namespace cursiv::engine {

// A symbol that wrote no record: nothing for a call site to rename.
inline constexpr std::uint32_t kLrNoWord = 0xffffffffu;

// What a reduce needs of a symbol it pops: its state, where its subtree opened,
// and where its record was written, as tape indices. The row is named only on
// reduction.
struct LrFrame {
  std::uint16_t state;
  // The row that record states so far.
  ElementId row;
  NodeCount open;
  TextOffset begin;
  std::uint32_t word;
};

static_assert(std::is_trivially_default_constructible_v<LrFrame>,
              "the stack allocates uninitialised and a push fills the frame");

}  // namespace cursiv::engine
