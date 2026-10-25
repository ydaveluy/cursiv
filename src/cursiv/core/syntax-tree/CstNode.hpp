#pragma once

#include <cstdint>
#include <limits>
#include <type_traits>

#include "cursiv/core/text/TextOffset.hpp"

// One record of the tape, and the vocabulary that addresses it.

namespace cursiv {

// One node inside one `Cst`. Ids follow commit order, which is postorder.
using NodeId = std::uint32_t;
inline constexpr NodeId kNoNode = std::numeric_limits<NodeId>::max();
// How many nodes -- a count, not a position. Same width, since a count is
// bounded by the ids it counts, and a separate name because the two are not
// interchangeable at a call site.
using NodeCount = std::uint32_t;

// Sixteen bytes. `begin`/`end` are offsets and not pointers, `subtreeSize` is
// the only structural word, and the predicates and the engine's stamp share
// the two bytes padding would have wasted.
struct CstNode {
  TextOffset begin;
  TextOffset end;
  // Postorder records in this subtree, root included. A childless record is
  // one; the last child of any other record is `id - 1`.
  NodeCount subtreeSize;
  // What the engine stamped on the record, opaque here: a tree is spans,
  // this structure and the two predicates below, and every other question
  // about a record goes to the grammar that engine generated. Nothing
  // outside it decodes these bits. FIRST IN THE WORD: an engine writes its
  // low half back on every reduction, and only a field at bit 0 is that
  // store rather than a read-modify-write.
  std::uint32_t engine : 30;
  // Whitespace and comments, recorded rather than dropped: a formatter and an
  // LSP need them, and one store beats re-lexing the document.
  std::uint32_t hidden : 1;
  // A node the parse repaired to produce: an error span the recovery
  // skipped, or a parent whose body needed one. The tree stays navigable;
  // the bit says which parts to trust.
  std::uint32_t recovered : 1;
};

// No ABI guard, unlike the AST's pins: every column is a `uint32`, so this
// says the same on a 32-bit target as on a 64-bit one.
static_assert(sizeof(CstNode) == 16, "the whole point is the size");
static_assert(alignof(CstNode) == 4);
static_assert(std::is_trivially_default_constructible_v<CstNode>,
              "the builder allocates uninitialised and fills in on exit");
static_assert(
    std::is_trivially_destructible_v<CstNode>,
    "rewinding must be a counter assignment, not a loop of destructors");
static_assert(std::is_trivially_copyable_v<CstNode>);

}  // namespace cursiv
