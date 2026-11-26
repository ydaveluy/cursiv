#pragma once

#include "cursiv/syntax-tree/AstNode.hpp"

namespace tindent::ast {
struct Node : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  vector<Node*> body;
};
struct Counted : cursiv::Extends<Node> {
  int64 dedents = 0;
};
}  // namespace tindent::ast
