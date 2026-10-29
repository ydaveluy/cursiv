#pragma once

#include <variant>

#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace teofregion {
struct Island : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> words;
};
struct Document : cursiv::Extends<cursiv::AstNode> {
  vector<std::variant<Island*, std::string_view>> parts;
};
struct Empty : cursiv::Extends<cursiv::AstNode> {};
struct ChainDocument : cursiv::Extends<cursiv::AstNode> {
  Empty* empty;
};
}

namespace tnullableregion {
struct Expr : cursiv::Extends<cursiv::AstNode> {};
struct Number : cursiv::Extends<Expr> {
  string_view text;
};
struct Block : cursiv::Extends<Expr> {
  Expr* value;
};
struct String : cursiv::Extends<Expr> {
  Block* hole;
};
}

namespace tregionalreadmode {
struct Node : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  Node* left;
  string_view op;
  Node* right;
  vector<Node*> args;
};
struct Text : cursiv::Extends<Node> {};
struct Block : cursiv::Extends<Node> {};
struct Echo : cursiv::Extends<Node> {};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Node*> items;
};
}

namespace tbytecond::ast {
struct Item : cursiv::Extends<cursiv::AstNode> {
  string_view text;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Item*> items;
};
struct Section : cursiv::Extends<cursiv::AstNode> {
  Doc* doc = nullptr;
};
}
