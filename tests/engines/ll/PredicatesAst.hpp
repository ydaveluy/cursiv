#pragma once

#include "cursiv/syntax-tree/AstNode.hpp"

namespace tpredicates {
struct Node : cursiv::Extends<cursiv::AstNode> {
  string_view text;
};
}  // namespace tpredicates
