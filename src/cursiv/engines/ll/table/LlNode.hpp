// One position of one body, as the program keeps it. The structure an author
// wrote survives here -- a repetition stays a repetition and an option stays
// an option -- because a predictive parser descends the shape instead of
// flattening it into productions.
#pragma once

#include <cstdint>

#include "cursiv/engines/shared/table/WriteKind.hpp"

namespace cursiv {

// `empty` takes no position and says nothing; `endMark` asserts end of input
// at a position, zero width, and takes none either.
enum class LlShape : std::uint8_t {
  term, call, seq, alt, rep, empty, endMark, condition, implied
};

struct LlNode {
  // The configuration a position is read under stands in `LlNodeBuild`:
  // construction reads it, a descent never does.
  LlShape kind{};
  // term: the terminal | call: the nonterminal | seq,alt: the kid base |
  // rep: the kid.
  std::uint16_t sym = 0;
  std::uint16_t count = 0;  // seq,alt: the kid count
  // rep: the turns, one of `(0, 1)`, `(0, kEndless)` and `(1, kEndless)`; the
  // lowering spells every other count out as copies of the body. implied:
  // `min` is the condition the token is laid on.
  std::uint32_t min = 0, max = 0;
  // What a directive standing over this position owes the record that position
  // produced. A directive takes no position of its own -- it says what the
  // position under it does.
  std::uint16_t row = kNoRow;
  // ...and whether the descent opens that record here rather than riding on
  // one the position hands back. A group always does; a call does only where
  // the rule it names lays none.
  bool owns = false;
  // The alphabet the lexer reads with here: what this position admits, and
  // the trivia it crosses. A token, an end mark, a condition and a call that
  // peeks its callee's first token carry one -- a choice's is its decision's.
  std::uint16_t mode = kNoRow;
  // The choice this position is: `alt` picks an arm, `rep` decides each turn.
  // `kNoRow` for everything that decides nothing.
  std::uint16_t decision = kNoRow;

  constexpr bool operator==(const LlNode&) const = default;
};

}  // namespace cursiv
