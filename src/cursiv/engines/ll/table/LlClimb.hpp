// A precedence table, as the descent climbs it: an operand rule, the operators
// that join two of them, and the three members one join writes. A chain of
// binary operators is not a body -- spelled as one it would be left-recursive,
// and spelled as a cascade it would be one near-identical rule per step.
#pragma once

#include <cstdint>

#include "cursiv/core/grammar/Association.hpp"
#include "cursiv/engines/shared/table/WriteKind.hpp"

namespace cursiv {

struct LlClimbOp {
  // The position reading this operator, including its condition and writes.
  std::uint16_t body = kNoRow;
  // Tightest highest, so a step's number is what the climb compares.
  std::uint8_t prec = 0;
  Association assoc = Association::left;

  constexpr bool operator==(const LlClimbOp&) const = default;
};

struct LlClimb {
  // The operand, as a rule. A grammar may climb over a TERMINAL instead --
  // `infix<NUMBER, ...>` -- and then the operand is a position of the program
  // like any other token, named here and not by a rule.
  std::uint16_t primary = kNoRow;
  std::uint16_t primaryNode = kNoRow;
  // The record one join lays, and what its three positions write into it.
  std::uint16_t node = kNoRow;
  std::uint16_t leftRow = kNoRow, rightRow = kNoRow;
  std::uint16_t opBase = 0, opCount = 0;
  // The alphabet an operator is read with.
  std::uint16_t mode = kNoRow;
  // The joins the three positions owe, as runs of the join pool: an operand
  // hands back a record, and the side it stands on states its member over it.
  std::uint32_t leftBase = 0, leftCount = 0;
  std::uint32_t rightBase = 0, rightCount = 0;
  // The state a call of the operand returns into: a prediction inside it
  // stands on the climb's own frame like any other.
  std::uint32_t atnEntry = 0;
  // A choice between operator bodies and leaving the climb, where operators
  // require more than the one-token dispatch above.
  std::uint16_t selectNode = kNoRow;

  constexpr bool operator==(const LlClimb&) const = default;
};

}  // namespace cursiv
