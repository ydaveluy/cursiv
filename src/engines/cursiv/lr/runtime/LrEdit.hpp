// One link of a repair search's path: an edit made where the table refused
// the token in hand, or an arm a reading took where it forked. The path is
// read through the links, so a rank is the choices along it.
#pragma once

#include <cstdint>

#include "cursiv/shared/runtime/PathRank.hpp"
#include "cursiv/shared/table/Token.hpp"
#include "cursiv/text/TextOffset.hpp"

namespace cursiv::engine {

struct LrEdit {
  // skip: a trivia the lexer refused, read as that trivia up to `end`.
  enum Kind : std::uint8_t { arm, ins, rep, del, skip, abandon };

  std::uint32_t parent = kNoEditLink;
  // Its order among the choices it was taken from: what a tie falls to.
  std::uint32_t choice = 0;
  Kind kind = arm;
  // The first edit of a fault: a real token was read since the last one.
  bool opens = false;
  // An arm taken while an invented token was still in hand: the writer
  // follows it, the drive does not see it.
  bool held = false;
  // ins, rep, skip: the terminal.
  SymbolId term = 0;
  // rep, del, skip: where the author's bytes it takes end. abandon: frames
  // kept.
  std::uint32_t end = 0;
  // Where the reading was when it made the edit: the pause a plan
  // recognises, by position, token and the stack above the frozen one.
  TextOffset at = 0;
  Token la{};
  std::uint32_t node = 0;
  std::uint32_t low = 0;
  // What the reading it was made from had cost.
  std::uint32_t cost = 0;
};

}  // namespace cursiv::engine
