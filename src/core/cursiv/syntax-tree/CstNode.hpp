#pragma once

#include <cstdint>
#include <limits>
#include <type_traits>

#include "cursiv/text/TextOffset.hpp"

// One CST record, and the types that address it.

namespace cursiv {

// One node inside one `Cst`. Ids follow the order records close: postorder.
using NodeId = std::uint32_t;
inline constexpr NodeId kNoNode = std::numeric_limits<NodeId>::max();
// How many nodes -- a count, not a position. Same width, since a count is
// bounded by the ids it counts, and a separate name because the two are not
// interchangeable at a call site.
using NodeCount = std::uint32_t;

// Sixteen bytes. `begin`/`end` are offsets and not pointers, `subtreeSize` is
// the only structural word, and the flags share a word with the engine's data.
struct CstNode {
  TextOffset begin;
  TextOffset end;
  // Postorder records in this subtree, root included. A childless record is
  // one; the last child of any other record is `id - 1`.
  NodeCount subtreeSize;
  // What the engine wrote on the record, decoded only by the grammar that
  // engine generated. First in the word, so an engine writes it with a plain
  // store.
  std::uint32_t engine : 30;
  // Whitespace and comments are kept: a formatter and the language server
  // need them.
  std::uint32_t hidden : 1;
  // A node the parse repaired to produce: an error span the recovery
  // skipped, or a parent whose body needed one. The tree stays navigable;
  // the bit says which parts to trust.
  std::uint32_t recovered : 1;
};

// No pointer-size guard, unlike the AST checks: every column is a `uint32`,
// so the size is the same on 32-bit and 64-bit targets.
static_assert(sizeof(CstNode) == 16, "the whole point is the size");
static_assert(alignof(CstNode) == 4);
static_assert(std::is_trivially_default_constructible_v<CstNode>,
              "the builder allocates uninitialised and fills in on exit");
static_assert(
    std::is_trivially_destructible_v<CstNode>,
    "rewinding must be a counter assignment, not a loop of destructors");
static_assert(std::is_trivially_copyable_v<CstNode>);

}  // namespace cursiv
