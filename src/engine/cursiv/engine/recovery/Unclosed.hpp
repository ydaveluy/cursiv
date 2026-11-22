// A token the lexer read past its match and was refused: a comment or a
// string that never closes. A repair reads it to where it would have ended.
#pragma once

#include <cstdint>

#include "cursiv/engine/recovery/ModeLexer.hpp"

namespace cursiv::engine {

struct Unclosed {
  // `kNoToken` where no body that waits for a closer was entered.
  std::uint16_t terminal = kNoToken;
  // Where its body began, past its opener, and where the reading ends.
  TextOffset open = 0;
  TextOffset end = 0;
  // Ended on its own closer; otherwise cut where its body refuses a line
  // end, or at the end of the text.
  bool closed = false;
};

// The reading at `p` as `mode` reads, none where its walk read no further
// than its match. The body it last entered goes on past the refused byte.
// A walk or guard that exhausts the budget gives no reading.
Unclosed unclosed(const ModeLexer& mode, TextOffset p, LexWork& work);

// What a deletion takes of the reading at `p`: a closed token whole, of one
// that never closed only its opener.
inline TextOffset unclosed_cut(TextOffset p, const Unclosed& reading) {
  if (reading.closed) return reading.end;
  return reading.open > p ? reading.open : p + 1;
}

}  // namespace cursiv::engine
