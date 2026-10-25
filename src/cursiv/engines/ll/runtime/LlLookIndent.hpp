#pragma once

#include <vector>

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/engines/shared/runtime/IndentLevel.hpp"

namespace cursiv {

// Lookahead advances its own indentation. It never changes the levels of
// the real descent while it tests an alternative.
struct LlLookIndent {
  std::vector<IndentLevel> arena;
  IndentAt at;
  TextOffset held = 0;
};

}  // namespace cursiv
