#pragma once

// A language's word on one node of the validation walk: whether its own checks
// run on it, and whether the walk goes on into what it contains. Both true is
// the default, and it is what makes the walk visit everything.

namespace cursiv {

struct ValidateSingleNodeOptions {
  bool validateNode = true;
  bool validateChildren = true;
};

inline constexpr ValidateSingleNodeOptions kValidateEachNode{};

}  // namespace cursiv
