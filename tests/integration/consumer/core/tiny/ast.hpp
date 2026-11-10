// `vector` is the alias `cursiv::AstNode` provides, as shipped grammars use it.
#pragma once

#include "cursiv/syntax-tree/AstNode.hpp"

namespace tiny::ast {

struct Term : cursiv::AstNode {
  int value = 0;
};

struct Sum : cursiv::AstNode {
  vector<Term*> terms;
};

}  // namespace tiny::ast
