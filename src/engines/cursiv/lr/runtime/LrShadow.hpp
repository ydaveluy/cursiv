// One reading of a cell the table left open: a run of states, a cursor, and
// the token in hand. Nothing here touches the tape: a reading is what the
// automaton would do, not what it would write.
#pragma once

#include <cstdint>

#include "cursiv/lr/runtime/LrDerivationStep.hpp"
#include "cursiv/lr/runtime/LrStackView.hpp"
#include "cursiv/lr/runtime/LrVirtualAt.hpp"
#include "cursiv/shared/runtime/IndentLevel.hpp"
#include "cursiv/shared/table/Token.hpp"

namespace cursiv::engine {

inline constexpr TextOffset kLrNoKeep = static_cast<TextOffset>(-1);

struct LrShadow {
  LrStackView run;

  TextOffset at = 0;
  // Everything a comparison reads but the run, in one number.
  std::uint64_t mark = 0;
  Token la{};
  std::uint16_t laMode = kNoMode;
  bool done = false;
  bool dead = false;
  // Shifted a token of the text since its round began.
  bool shifted = false;
  // The token in hand's weight once a tie of extents read it.
  static constexpr std::uint8_t kUnweighed = 0xFF;
  std::uint8_t laWeight = kUnweighed;
  LrVirtualAt virt;
  IndentAt indent;
  // Where the token in hand was counted against the look depth.
  TextOffset keptAt = kLrNoKeep;
  // The word the token in hand's lexical rank adds to a derivation, where
  // the lexer ranked competitors apart; zero for none.
  std::uint32_t lexRank = 0;
  // The last step of its derivation in the cell's derivation steps and its
  // path as the cell sees it, the arm that derivation descends from, and
  // whether readings of that arm met without the rule electing between them.
  std::uint32_t derivation = kLrNoDerivation;
  std::uint64_t path = 0;
  std::uint16_t arm = 0;
  bool torn = false;
};

}  // namespace cursiv::engine
