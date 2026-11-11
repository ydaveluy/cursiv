// One position of one body as the program keeps it. The written structure
// survives, since a predictive parser descends the shape, not productions.
#pragma once

#include <cstdint>

#include "cursiv/shared/table/WriteKind.hpp"

namespace cursiv::engine {

// `empty` takes no position and says nothing; `endMark` asserts end of input
// at a position, zero width, and takes none either.
enum class LlShape : std::uint8_t {
  term, call, seq, alt, rep, empty, endMark, condition, implied
};

struct LlNode {
  LlShape kind{};
  // term: the terminal | call: the nonterminal | seq,alt: the child base |
  // rep: the child.
  std::uint16_t sym = 0;
  std::uint16_t count = 0;  // seq,alt: the child count
  // rep: the turns, one of `(0, 1)`, `(0, kEndless)` and `(1, kEndless)`; the
  // lowering expands every other count into copies of the body. implied:
  // `min` is the token's condition.
  std::uint32_t min = 0, max = 0;
  // What a directive over this position writes into the record that position
  // produced. A directive takes no position of its own; it says what the
  // position under it does.
  std::uint16_t row = kNoRow;
  // Whether the descent opens that record here rather than reusing one the
  // position hands back. A group always does; a call does only where the rule
  // it names creates none.
  bool owns = false;
  // The alphabet the lexer reads with here: what this position admits, and
  // the trivia it crosses. A token, an end mark, a condition and a call that
  // peeks its callee's first token carry one; a choice's is its decision's.
  std::uint16_t mode = kNoRow;
  // The choice this position is: `alt` picks an arm, `rep` decides each turn.
  // `kNoRow` for everything that decides nothing.
  std::uint16_t decision = kNoRow;
  // The skip configuration the position is read under: its region, zero for the
  // grammar's. A rule whose body opens a region has its own interior and the
  // caller's entry, so this belongs to the position.
  std::uint8_t cfg = 0;
  // The section a call opens, its configuration plus one: the descent enters
  // it reading that grammar alone.
  std::uint8_t section = 0;
  // The repetition of the turns of a left recursion.
  bool leftFold = false;
  // A normalized fold closes each turn over the already consumed prefix.
  bool foldStep = false;
  // A nest over one terminal: the row that terminal carries where the nest
  // has no receiver to wrap and makes no record, its own and the nest's in one.
  std::uint16_t leafRow = kNoRow;
  // A turn of a counted repetition's optional tail, and the sequence that
  // joins it to the next: the tail is one chain, written out flat.
  bool countedTail = false;

  constexpr bool operator==(const LlNode&) const = default;
};

}  // namespace cursiv::engine
