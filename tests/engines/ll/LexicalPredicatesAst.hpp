#pragma once

#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tlexicalpredicates {
struct Node : cursiv::Extends<cursiv::AstNode> {
  string_view text;
};
}  // namespace tlexicalpredicates
