#pragma once

// The one node the dangling-else grammars build, shared so a lane can walk
// either declaration order with the same hand-written check.

#include <string_view>

#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tdang::ast {

struct Stmt : cursiv::AstNode {
  std::string_view cond;
  Stmt* then_ = nullptr;
  Stmt* else_ = nullptr;
};

}  // namespace tdang::ast
