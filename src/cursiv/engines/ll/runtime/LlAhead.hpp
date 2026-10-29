#pragma once

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/engines/shared/runtime/IndentLevel.hpp"

namespace cursiv {

// A read of the text ahead of the descent: where the next token is read from,
// and the levels it stands in, in an arena its reader keeps. `head` is set
// where nothing but the last token stands in front, the only place a line's
// indentation is read.
struct LlAhead {
  IndentAt at;
  TextOffset held = 0;
  TextOffset p = 0;
  bool head = true;
};

}  // namespace cursiv
