#pragma once

#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tllswiftwidth {
struct Raw : cursiv::Extends<cursiv::AstNode> {
  string_view opening;
};
struct Document : cursiv::Extends<cursiv::AstNode> {
  vector<Raw*> values;
};
}  // namespace tllswiftwidth
