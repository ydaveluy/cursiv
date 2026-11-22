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

// The element of text a repair skipped: a recovered leaf read as nothing of
// the grammar.
inline constexpr std::uint32_t kSkippedElement = 0x3FFFFFFFu;

// Sixteen bytes. `begin`/`end` are offsets and not pointers, `subtreeSize` is
// the only structural word, and the element shares a word with the flags.
struct CstNode {
  TextOffset begin;
  TextOffset end;
  // Postorder records in this subtree, root included. A childless record is
  // one; the last child of any other record is `id - 1`.
  NodeCount subtreeSize;

  // The whole word: the element and both flags.
  [[nodiscard]] constexpr std::uint32_t engine() const noexcept { return _engine; }
  // The element of the grammar the record was read as, a number its
  // language's tables give a meaning to, with whether it is hidden: trivia
  // stands past every element, so one comparison with a count of elements
  // leaves it out.
  [[nodiscard]] constexpr std::uint32_t element() const noexcept {
    return _engine & kElementMask;
  }
  // Whitespace and comments are kept: a formatter and the language server
  // need them.
  [[nodiscard]] constexpr bool isHidden() const noexcept { return (_engine & kHiddenFlag) != 0; }
  // A node the parse repaired to produce: an error span the recovery
  // skipped, or a parent whose body needed one. The tree stays navigable;
  // the flag says which parts to trust.
  [[nodiscard]] constexpr bool isRecovered() const noexcept {
    return (_engine & kRecoveredFlag) != 0;
  }

  // On a new record `setEngine` comes first: the other setters read the
  // word.
  constexpr void setEngine(std::uint32_t value) noexcept { _engine = value; }
  // Replaces the element and whether the record is hidden; a repair's flag
  // stays.
  constexpr void setElement(std::uint32_t value) noexcept {
    _engine = (_engine & kRecoveredFlag) | (value & kElementMask);
  }
  constexpr void setHidden(bool value) noexcept {
    _engine = (_engine & ~kHiddenFlag) | (value ? kHiddenFlag : 0u);
  }
  constexpr void setRecovered(bool value) noexcept {
    _engine = (_engine & ~kRecoveredFlag) | (value ? kRecoveredFlag : 0u);
  }

 private:
  static constexpr std::uint32_t kHiddenFlag = std::uint32_t{1} << 30;
  static constexpr std::uint32_t kRecoveredFlag = std::uint32_t{1} << 31;
  static constexpr std::uint32_t kElementMask = ~kRecoveredFlag;

  std::uint32_t _engine;
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
