// One caret lane, run against whichever engine generated the caret grammar:
// the module's two doors are all the lane needs.
#pragma once

#include <memory>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"

struct CaretDoor {
  std::unique_ptr<cursiv::Parser> (*parser)();
  std::unique_ptr<cursiv::AstReflection> (*reflection)();
};

void run_caret_lane(const CaretDoor& door);
