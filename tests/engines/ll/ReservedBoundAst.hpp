#pragma once

#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tllbound {
struct Item : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct Flag : cursiv::Extends<cursiv::AstNode> {
  bool value = false;
};
struct Mark : cursiv::Extends<cursiv::AstNode> {
  string_view one;
  string_view two;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Item*> items;
  vector<Flag*> flags;
  vector<Mark*> marks;
};
}  // namespace tllbound
