// Completion tests on the caret grammar: they need only a generated module's
// parser and reflection, so any engine's module can run them.
#pragma once

#include <memory>

#include "cursiv/parser/Parser.hpp"
#include "cursiv/syntax-tree/AstReflection.hpp"

struct CaretEngine {
  std::unique_ptr<cursiv::Parser> (*parser)();
  std::unique_ptr<cursiv::AstReflection> (*reflection)();
};

void run_caret_lane(const CaretEngine& engine);
