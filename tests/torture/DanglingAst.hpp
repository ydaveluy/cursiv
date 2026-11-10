#pragma once

// The one node the dangling-else grammars build, shared so a test can walk
// either declaration order with the same check.

#include <string_view>

#include "cursiv/syntax-tree/AstNode.hpp"

namespace tdang::ast {

struct Stmt : cursiv::Extends<cursiv::AstNode> {
  string_view cond;
  Stmt* then_ = nullptr;
  Stmt* else_ = nullptr;
};

}  // namespace tdang::ast
