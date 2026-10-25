// What construction reads of a production and the frozen table never does:
// beside the cell, not in it.
#pragma once

#include <cstdint>

#include "cursiv/engines/shared/table/WriteKind.hpp"

namespace cursiv {

struct LrProdBuild {
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
  // Empty where a region closes: it stands on the boundary past the region,
  // not on the caller's in front of it.
  bool closesRegion = false;
  // The empty production that stops a commit's loop on a token no cut names:
  // its cut plus one, zero for any other.
  std::uint16_t stop = 0;
  // The section the rule is read in, its configuration; zero outside any.
  std::uint8_t home = 0;
  // A nest turn whose record reaches back over its receiver, the record the
  // frame below it laid.
  bool turn = false;
  std::uint16_t turnNode = kNoRow;
  // One order of an unordered group, whose rule is the group's alone: of two
  // orders spelling the same symbols, the earlier one is elected.
  bool permuted = false;

  constexpr bool operator==(const LrProdBuild&) const = default;
};

}  // namespace cursiv
