#pragma once

#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tllunit {
struct Document : cursiv::Extends<cursiv::AstNode> {
  string_view padding;
};
struct Name : cursiv::Extends<cursiv::AstNode> {
  string_view text;
};
struct Stmts : cursiv::Extends<cursiv::AstNode> {
  vector<Name*> stmts;
};
}  // namespace tllunit
