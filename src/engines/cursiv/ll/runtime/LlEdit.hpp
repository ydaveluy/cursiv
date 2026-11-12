// One link of a repair search's path: an edit made where the readings were
// refused. The path is read through the links, so a rank is the choices along
// it.
#pragma once

#include <cstdint>

#include "cursiv/shared/runtime/PathRank.hpp"
#include "cursiv/shared/table/Token.hpp"
#include "cursiv/text/TextOffset.hpp"

namespace cursiv::engine {

struct LlEdit {
  // skip: a trivia the lexer refused, read as that trivia up to `end`.
  enum Kind : std::uint8_t { ins, rep, del, skip, abandon };

  std::uint32_t parent = kNoEditLink;
  // Its order among the choices it was taken from: what a tie falls to.
  std::uint32_t choice = 0;
  Kind kind = ins;
  // ins, rep, skip: the terminal. abandon: what the call still needed, or
  // `kNoElement` where no one terminal says it.
  SymbolId term = 0;
  // rep, del, skip: where the author's bytes it takes end. abandon: how many
  // calls it leaves.
  TextOffset end = 0;
  // abandon: exact return state.
  std::uint32_t state = 0;
  // The pause it was made at: where the text taken stands, and where the token
  // in hand begins.
  TextOffset at = 0;
  TextOffset begin = 0;
  // The author's tokens its path dropped, itself included.
  std::uint32_t dropped = 0;
};

}  // namespace cursiv::engine
