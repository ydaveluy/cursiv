#pragma once

#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tllkwtrivia {
struct Part : cursiv::Extends<cursiv::AstNode> {
  string_view text;
};
struct Value : cursiv::Extends<cursiv::AstNode> {};
struct Ref : cursiv::Extends<Value> {
  string_view text;
};
struct Quotient : cursiv::Extends<Value> {
  Value* left{};
  Value* right{};
};
struct Assign : cursiv::Extends<cursiv::AstNode> {
  string_view target;
  vector<Part*> parts;
  Value* value{};
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Assign*> statements;
};
struct Parts : cursiv::Extends<cursiv::AstNode> {
  vector<Part*> parts;
};
}  // namespace tllkwtrivia
