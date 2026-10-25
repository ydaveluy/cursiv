// One link of a repair search's path: a gesture made where the table refused
// the token in hand, or an arm a reading took where it forked. The path is
// read through the links, so a rank is the choices along it.
#pragma once

#include <cstdint>

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/engines/shared/runtime/GestureRank.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

struct LrGesture {
  // skip: a trivia the lexer refused, read as that trivia up to `end`.
  enum Kind : std::uint8_t { arm, ins, rep, del, skip, abandon };

  std::uint32_t parent = kNoGestureLink;
  // Its order among the choices it was taken from: what a tie falls to.
  std::uint32_t choice = 0;
  Kind kind = arm;
  // The first gesture of a fault: a real token was read since the last one.
  bool opens = false;
  // An arm taken while an invented token was still in hand: the writer
  // follows it, the drive does not see it.
  bool held = false;
  // ins, rep, skip: the terminal. del: the one left in hand after it.
  SymbolId term = 0;
  // rep, del, skip: where the author's bytes it takes end. abandon: frames
  // kept.
  std::uint32_t end = 0;
  // Where the reading stood when it made the gesture: the pause a plan
  // recognises, by position, token and the stack above the frozen one.
  TextOffset at = 0;
  Token la{};
  std::uint32_t node = 0;
  std::uint32_t low = 0;
  // What the reading it was made from had paid.
  std::uint32_t cost = 0;
};

}  // namespace cursiv
