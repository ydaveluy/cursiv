#pragma once

#include <vector>

#include "cursiv/text/TextOffset.hpp"
#include "cursiv/shared/runtime/IndentLevel.hpp"

namespace cursiv::engine {

// Lookahead advances its own indentation. It never changes the levels of
// the real descent while it tests an alternative.
struct LlLookIndent {
  std::vector<IndentLevel> arena;
  IndentAt at;
  TextOffset held = 0;
};

}  // namespace cursiv::engine
