#pragma once

#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tlllatewidth {
struct Word : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct Value : cursiv::Extends<cursiv::AstNode> {
  vector<Word*> prefix;
};
struct NumberValue : cursiv::Extends<Value> {
  string_view number;
};
struct DimensionValue : cursiv::Extends<Value> {
  string_view dimension;
};
struct Document : cursiv::Extends<cursiv::AstNode> {
  Value* value = nullptr;
};
}  // namespace tlllatewidth
