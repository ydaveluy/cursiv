#pragma once

#include "cursiv/syntax-tree/AstNode.hpp"

namespace tllunit {
struct Document : cursiv::Extends<cursiv::AstNode> {
  string_view padding;
};
struct Name : cursiv::Extends<cursiv::AstNode> {
  string text;
};
struct Stmts : cursiv::Extends<cursiv::AstNode> {
  vector<Name*> stmts;
};
}  // namespace tllunit
