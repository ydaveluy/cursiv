// One link of a repair search's path: a gesture made where the readings were
// refused. The path is read through the links, so a rank is the choices along
// it.
#pragma once

#include <cstdint>

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/engines/shared/runtime/GestureRank.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

struct LlGesture {
  // skip: a trivia the lexer refused, read as that trivia up to `end`.
  enum Kind : std::uint8_t { ins, rep, del, skip, abandon };

  std::uint32_t parent = kNoGestureLink;
  // Its order among the choices it was taken from: what a tie falls to.
  std::uint32_t choice = 0;
  Kind kind = ins;
  // ins, rep, skip: the terminal.
  SymbolId term = 0;
  // rep, del, skip: where the author's bytes it takes end.
  TextOffset end = 0;
  // abandon: exact return state; `end` is how many calls are left.
  std::uint32_t state = 0;
  // The pause it was made at: where the text taken stands, and where the token
  // in hand begins.
  TextOffset at = 0;
  TextOffset begin = 0;
};

}  // namespace cursiv
