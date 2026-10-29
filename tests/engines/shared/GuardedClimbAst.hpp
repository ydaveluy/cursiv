#pragma once

#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tguardedclimb {
struct Expr : cursiv::Extends<cursiv::AstNode> {};
struct Ident : cursiv::Extends<Expr> {
  string_view name;
};
struct Number : cursiv::Extends<Expr> {
  string_view text;
};
struct Binary : cursiv::Extends<Expr> {
  Expr* left;
  string_view op;
  Expr* right;
};
struct Call : cursiv::Extends<Expr> {
  Expr* callee;
  vector<Expr*> args;
};
struct Postfix : cursiv::Extends<Expr> {
  Expr* operand;
  string_view name;
};
struct Bind : cursiv::Extends<Expr> {
  string_view name;
};
struct Part : cursiv::Extends<cursiv::AstNode> {
  string_view text;
  string_view name;
};
struct Pattern : cursiv::Extends<Expr> {
  string_view prefix;
  vector<Part*> parts;
};
struct Match : cursiv::Extends<Expr> {
  Expr* operand;
  Expr* pattern;
  Expr* body;
};
struct Generator : cursiv::Extends<cursiv::AstNode> {
  Expr* pattern;
  Expr* source;
};
struct For : cursiv::Extends<Expr> {
  vector<Generator*> generators;
  Expr* body;
};
}  // namespace tguardedclimb
