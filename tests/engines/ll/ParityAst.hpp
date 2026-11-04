#pragma once

#include "cursiv/syntax-tree/AstNode.hpp"

namespace tpar::ast {
struct Expr : cursiv::Extends<cursiv::AstNode> {};
struct Num : cursiv::Extends<Expr> {
  double value{};
};
struct Name : cursiv::Extends<Expr> {
  string_view id;
};
struct Call : cursiv::Extends<Expr> {
  string_view name;
  vector<Expr*> args;
};
struct Binary : cursiv::Extends<Expr> {
  Expr* left{};
  string_view op;
  Expr* right{};
};
struct Neg : cursiv::Extends<Expr> {
  Expr* operand{};
};
struct Fact : cursiv::Extends<Expr> {
  Expr* operand{};
};
struct Let : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  Expr* guard{};
  Expr* value{};
  bool exported{};
};
struct Item : cursiv::Extends<cursiv::AstNode> {
  Expr* value{};
  string_view alias;
};
struct Show : cursiv::Extends<cursiv::AstNode> {
  vector<Item*> items;
  bool all{};
};
struct Unit : cursiv::Extends<cursiv::AstNode> {
  vector<Let*> lets;
  vector<Show*> shows;
};
struct Lit : cursiv::Extends<Expr> {};
struct Chained : cursiv::Extends<Expr> {
  Expr* left{};
  string_view name;
};
struct Hold : cursiv::Extends<cursiv::AstNode> {
  Expr* first{};
  Expr* second{};
  string_view text;
  vector<Expr*> marks;
  vector<Hold*> turns;
};
struct Holds : cursiv::Extends<cursiv::AstNode> {
  vector<Hold*> holds;
};
}  // namespace tpar::ast
