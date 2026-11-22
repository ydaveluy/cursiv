#pragma once

#include "cursiv/syntax-tree/AstNode.hpp"

namespace tasi::ast {
struct Stmt : cursiv::Extends<cursiv::AstNode> {
  string_view head;
  vector<Stmt*> args;
  vector<Stmt*> body;
  boolean ret = false;
  boolean empty = false;
  boolean inc = false;
  boolean go = false;
  vector<Stmt*> divs;
  boolean obj = false;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Stmt*> body;
};
}  // namespace tasi::ast

namespace tasi::ops::ast {
struct Node : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  string_view op;
  vector<Node*> children;
};
struct Pre : cursiv::Extends<Node> {};
struct Post : cursiv::Extends<Node> {};
struct Bin : cursiv::Extends<Node> {};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Node*> items;
};
}  // namespace tasi::ops::ast

namespace tasi::tail::ast {
struct Node : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  string_view op;
  vector<Node*> children;
};
struct Bin : cursiv::Extends<Node> {};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Node*> items;
};
}  // namespace tasi::tail::ast

namespace tasi::block::ast {
struct Node : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  vector<Node*> body;
};
struct Block : cursiv::Extends<Node> {};
struct Var : cursiv::Extends<Node> {};
struct Obj : cursiv::Extends<Node> {};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Node*> items;
};
}  // namespace tasi::block::ast

namespace tasi::optgap::ast {
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> names;
};
}  // namespace tasi::optgap::ast

namespace tasi::prec::ast {
struct Expr : cursiv::Extends<cursiv::AstNode> {};
struct Bin : cursiv::Extends<Expr> {
  Expr* left = nullptr;
  string_view op;
  Expr* right = nullptr;
};
struct Name : cursiv::Extends<Expr> {
  string_view name;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Expr*> items;
};
}  // namespace tasi::prec::ast
