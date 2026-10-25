// One reading of a cell the table left open: a run of states, a cursor, and
// the token in hand. Nothing here touches the tape -- a reading is what the
// automaton would do, not what it would write.
#pragma once

#include <cstdint>

#include "cursiv/engines/lr/runtime/LrStackClimb.hpp"
#include "cursiv/engines/lr/runtime/LrTrailStep.hpp"
#include "cursiv/engines/lr/runtime/LrVirtualAt.hpp"
#include "cursiv/engines/shared/runtime/IndentLevel.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

inline constexpr TextOffset kLrNoKeep = static_cast<TextOffset>(-1);

struct LrShadow {
  LrStackClimb run;

  TextOffset at = 0;
  // Everything a comparison reads but the run, in one number.
  std::uint64_t mark = 0;
  Token la{};
  std::uint16_t laMode = kNoMode;
  bool done = false;
  bool dead = false;
  // Shifted a token of the text since its round began.
  bool shifted = false;
  LrVirtualAt virt;
  IndentAt indent;
  // Where the token in hand was charged to the look depth.
  TextOffset keptAt = kLrNoKeep;
  // The word the token in hand's lexical rank adds to a derivation, where
  // the lexer ranked rivals apart; zero for none.
  std::uint32_t lexRank = 0;
  // The last step of its derivation in the cell's trail and its path spelled
  // as the cell sees it, the arm that derivation descends from, and whether
  // readings of that arm met without the rule electing between them.
  std::uint32_t trail = kLrNoTrail;
  std::uint64_t path = 0;
  std::uint16_t arm = 0;
  bool torn = false;
};

}  // namespace cursiv
