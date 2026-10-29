#pragma once

#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tindent::ast {
struct Node : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  vector<Node*> body;
};
}  // namespace tindent::ast
