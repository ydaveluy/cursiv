// Repeating a flag writes the same scalar on each turn.
// expect LR: WrittenEachTurn<.*refuse::G::Root.*Item::on
#pragma once
#include "refusals/head.hpp"
namespace refuse {
namespace ast {
struct Item : cursiv::Extends<cursiv::AstNode> {
  boolean on = false;
};
}  // namespace ast
struct G : Base {
  struct Root : rule<some(flag<&ast::Item::on>(lit<"!">))> {};
  using entry = Root;
};
}  // namespace refuse
