#pragma once

#include "cursiv/syntax-tree/AstNode.hpp"

namespace twide::ast {

struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> words;
};

}  // namespace twide::ast
