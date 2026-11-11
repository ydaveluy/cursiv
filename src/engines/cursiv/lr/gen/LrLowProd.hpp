// One production, and one position of a right-hand side: a production's
// positions are a run of the configuration's slot pool.
#pragma once

#include <cstdint>

#include "cursiv/shared/table/WriteKind.hpp"

namespace cursiv::engine {

struct LrLowProd {
  std::uint16_t lhs = 0;  // nonterminal index
  // The actions this reduce applies to the record it creates; `kNoRow` when it
  // creates none and hands them to the record below instead.
  std::uint16_t node = kNoRow;
  std::uint32_t at = 0;  // where its positions start in the pool
  std::uint16_t len = 0;

  // Whether this reduce can read a chain already in hand.
  bool folds = false;
  // Reduced only where condition `gap - 1` holds; 0 for every other.
  std::uint8_t gap = 0;

  // The source alternative and the greedy subset inside it: a larger subset
  // wins only inside its own alternative.
  std::uint16_t priorityAlt = kNoRow;
  std::uint16_t prioritySubset = 0;
  // The options it decided: `decisions[decideAt, +decideLen)`
  // of the configuration, each `position << 16 | arm`.
  std::uint32_t decideAt = 0;
  std::uint32_t decideLen = 0;
  // The rule an author wrote, even where `lhs` is an auxiliary.
  std::uint16_t owner = kNoRow;
  std::uint8_t prec = 0;
  // End of input asserted after the last symbol.
  bool endOnly = false;
  // The configuration the boundary after the last symbol is read under.
  std::uint8_t tailCfg = 0;
  // Empty, or conditions alone, where a region closes: it stands on the
  // boundary past the region, not on the caller's in front of it.
  bool closesRegion = false;
  // This reduction returns a gap to its caller. Kept before a leading
  // caller edge can hide the region's own configuration in its positions.
  bool regionExit = false;
  // The empty production that stops a commit's loop on a token no cut names:
  // its cut plus one, zero for any other.
  std::uint16_t stop = 0;
  // The section the rule is read in, its configuration; zero outside any.
  std::uint8_t sectionCfg = 0;
  // A nest turn whose record reaches back over its receiver, the record the
  // frame below it created.
  bool turn = false;
  std::uint16_t turnNode = kNoRow;
  // One order of an unordered group, whose rule is the group's alone: of two
  // orders with the same symbols, the earlier one is elected.
  bool permuted = false;

  constexpr bool operator==(const LrLowProd&) const = default;
};

struct LrLowSlot {
  // The actions the call site adds to the record this position produced.
  std::uint16_t row = kNoRow;
  std::uint16_t sym = 0;
  // The configuration the boundary in front of it is read under.
  std::uint8_t cfg = 0;
  // The condition it is passed under plus one (`lr_guard_positions`).
  std::uint8_t guard = 0;

  constexpr bool operator==(const LrLowSlot&) const = default;
};

}  // namespace cursiv::engine
