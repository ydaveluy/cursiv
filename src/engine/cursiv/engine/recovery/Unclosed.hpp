// A token the lexer read past its match and was refused: a comment or a
// string that never closes. A repair reads it to where it would have ended.
#pragma once

#include <cstdint>
#include <vector>

#include "cursiv/engine/recovery/ModeLexer.hpp"

namespace cursiv::engine {

struct Unclosed {
  // The terminal it reads as, `kNoToken` for none; one that never closed in
  // a body of the automaton waits in `body`, which the reading state names.
  std::uint16_t terminal = kNoToken;
  std::uint16_t body = 0;
  // Where its body began, past its opener, and where the reading ends.
  TextOffset open = 0;
  TextOffset end = 0;
  // Ended on its own closer; otherwise cut where its body refuses a line
  // end, or at the end of the text.
  bool closed = false;
};

// The lexer states a walk asked its mode about, each once: a mode that reads
// alike in them walks alike.
struct LexVisits {
  std::vector<std::uint8_t> seen;
  std::vector<std::uint16_t> states;
  void add(std::uint16_t s) {
    if (s >= seen.size()) seen.resize(s + 1, 0);
    if (seen[s] != 0) return;
    seen[s] = 1;
    states.push_back(s);
  }
  void clear() {
    for (const std::uint16_t s : states) seen[s] = 0;
    states.clear();
  }
};

// The reading at `p` as `mode` reads, none where its walk read no further
// than its match. The body it last entered goes on past the refused byte;
// which terminal waits there is the reading state's to say. A terminal the
// matcher reads that the text ends inside is read up to the end. A walk or
// guard that exhausts the budget gives no reading.
Unclosed unclosed(const ModeLexer& mode, TextOffset p, LexWork& work, LexVisits& visits);

// What a deletion takes of the reading at `p`: a closed token whole, of one
// that never closed only its opener.
inline TextOffset unclosed_cut(TextOffset p, const Unclosed& reading) {
  if (reading.closed) return reading.end;
  return reading.open > p ? reading.open : p + 1;
}

}  // namespace cursiv::engine
