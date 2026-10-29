#pragma once

#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tllrescan {
struct Item : cursiv::Extends<cursiv::AstNode> {
  string_view text;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Item*> items;
};
}  // namespace tllrescan
