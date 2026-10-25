// The extent of the lowering a frozen table keeps.
#pragma once

namespace cursiv {

struct LrLowSizes {
  unsigned nameChars = 0, sets = 0, terms = 0, prods = 0, wordRanges = 0,
           scalarRanges = 0;

  constexpr bool operator==(const LrLowSizes&) const = default;
};

}  // namespace cursiv
