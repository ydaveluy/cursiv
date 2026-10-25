// What the finished table says about itself: the counts a parse reads and the
// byte-to-class map. Construction keeps its own facts in `LrBuildTable`.
#pragma once

#include <cstdint>

#include "cursiv/engines/shared/table/StampLayout.hpp"

namespace cursiv {

struct LrTableFacts {
  std::uint16_t nstates = 0;

  std::uint32_t nconflicts = 0;
  // Conflict cells the second token decides: the drive peeks, not forks.
  std::uint32_t npeeks = 0;

  // Lexer: byte classes shared by every mode, one DFA per mode.
  std::uint8_t cls[256]{};
  std::uint8_t ncls = 0;
  bool multiCand = false;
  // The mode that lexes everything, for a repair to name what a state could
  // not lex.
  std::uint16_t errMode = 0;
  // Terminals one position may match at once.
  std::uint16_t maxCands = 0;
  // Arms of the widest cell the look-ahead is handed: it sizes the reading's
  // bookkeeping.
  std::uint16_t maxArms = 0;
  StampLayout stamp{};
};

}  // namespace cursiv
