// What construction reads of a terminal, a rule and a position of a body, and
// the frozen program never does. Beside the cells and not in them: only the
// lowering holds these, and a table is emitted without them.
#pragma once

#include <cstdint>

#include "cursiv/engines/shared/table/WriteKind.hpp"

namespace cursiv {

struct LlRuleBuild {
  // It stands on a cycle that consumes nothing before reaching itself.
  bool leftCyclic = false;

  constexpr bool operator==(const LlRuleBuild&) const = default;
};

// The cost of an arm no token can stand for: it asserts the end of the text.
inline constexpr std::uint16_t kLlRepairCostless = 0xFFFFu;

struct LlDecisionBuild {
  // The arms it decides between: the emitted program spells each of them, so
  // a descent never counts them.
  std::uint16_t nalts = 0;
  // What the cheapest arm costs in invented tokens. A choice whose cheapest
  // arm is a whole construct is skipped instead: a member the author did not
  // write is not a repair, it is a second document -- unless the text has
  // ended, where no token is left to skip. The emitter reads it to decide
  // whether to write the repair at all.
  std::uint16_t repairCost = 0;

  constexpr bool operator==(const LlDecisionBuild&) const = default;
};

struct LlNodeBuild {
  // The skip configuration the position is read under: the region it stands
  // in, zero for the grammar's. The regime rides the position -- a rule whose
  // body opens a region has an interior of its own and the caller's entry,
  // which no per-rule flag can say.
  std::uint8_t cfg = 0;
  // The section a call opens, its configuration plus one: the descent enters
  // it reading that grammar alone.
  std::uint8_t section = 0;
  // The repetition of the turns of a left recursion.
  bool leftFold = false;
  // A normalized fold closes each turn over the already consumed prefix.
  bool foldStep = false;
  // A nest over one terminal: the row that terminal carries where the nest
  // has no receiver to wrap and lays no record, its own and the nest's in one.
  std::uint16_t leafRow = kNoRow;
  // A turn of a counted repetition's optional tail, and the sequence that
  // joins it to the next: the tail is one chain, written out flat.
  bool ladder = false;
  // The rule whose repetition of more than one turn repeats this item: the
  // item may not match nothing.
  std::uint16_t repeatOwner = kNoRow;
  // A choice the election ranks: arms that spell alike are told apart by it.
  bool ranked = false;

  constexpr bool operator==(const LlNodeBuild&) const = default;
};

}  // namespace cursiv
