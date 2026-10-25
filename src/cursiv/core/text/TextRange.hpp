#pragma once

#include "cursiv/core/text/TextOffset.hpp"

// A span of the source, in bytes: what the core measures and reports.
// `Range` is the same extent in the client's unit; conversion happens at the
// publishing frontier, never here.

namespace cursiv {

// Half-open, like every published range.
struct TextRange {
  TextOffset begin = 0, end = 0;
};

}  // namespace cursiv
