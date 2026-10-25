// One symbol's frame on the parse stack.
#pragma once

#include <cstdint>
#include <type_traits>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"

namespace cursiv {

// A symbol that wrote no record: nothing for a call site to rename.
inline constexpr std::uint32_t kLrNoWord = 0xffffffffu;

// What a reduce needs of a symbol it pops: its state, where its subtree
// opened (closing a node is a subtraction), and where its record was written
// -- tape indices. The row is named only on reduction: one state serves every
// call site of a symbol.
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

}  // namespace cursiv
