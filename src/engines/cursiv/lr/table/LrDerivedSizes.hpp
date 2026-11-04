// The extent of the tables derived from a built one, beyond `LrSizes`.
#pragma once

namespace cursiv {

struct LrDerivedSizes {
  unsigned lexElseRows = 0;
  unsigned typoSucc = 0;
  unsigned reduceNames = 0;
  // The continuations summed over the states, and the distinct ones.
  unsigned expectItems = 0;
  unsigned expectPool = 0;
  unsigned rowLhsRows = 0;
  unsigned rowLhs = 0;

  unsigned typoReachSets = 0;
  unsigned afterSets = 0;
  unsigned modeWantSets = 0;

  constexpr bool operator==(const LrDerivedSizes&) const = default;
};

}  // namespace cursiv
