// The consumer's AST: one node, one member. `vector`/`string_view` are the
// base's aliases, as every shipped grammar spells them.
#pragma once

#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tiny::ast {

struct Term : cursiv::AstNode {
  int value = 0;
};

struct Sum : cursiv::AstNode {
  vector<Term*> terms;
};

}  // namespace tiny::ast
