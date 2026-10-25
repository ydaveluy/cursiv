// A random grammar over the tokens `a` to `d`: rules of choices, sequences,
// repetitions, conditions and `skip()` regions. The seed draws it and picks
// the family of features it is drawn in.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "oracle/RgramExpr.hpp"

namespace cursiv {

struct RgramGrammar {
  struct Features {
    bool conditions = true;
    bool ast = false;
    bool regions = false;
    bool closedRegions = false;
    bool commits = false;
    bool optionalRuns = false;
    bool eofChoice = false;
  };

  unsigned seed = 0;
  std::string_view family{};
  Features features{};
  // Rule `i` is `R<i>`, a choice; `R0` is the entry.
  std::vector<RgramExpr> rules{};

  static RgramGrammar draw(unsigned seed);
  // The header that declares it as `rgram<seed>::G`.
  std::string header() const;
};

}  // namespace cursiv
