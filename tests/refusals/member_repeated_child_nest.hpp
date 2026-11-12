// A nest inside the node written into `item` starts no new `Holder`: each turn
// writes `item` again.
// expect LR at compile: WrittenEachTurn<.*refuse::G::Root.*refuse::ast::Holder::item
#pragma once
#include "refusals/head.hpp"
namespace refuse {
namespace ast {
struct Item : cursiv::Extends<cursiv::AstNode> {
  string_view text;
  Item* previous = nullptr;
};
struct Holder : cursiv::Extends<cursiv::AstNode> {
  Item* item = nullptr;
};
}  // namespace ast
struct G : Base {
  struct Root : rule<create<ast::Holder>(some(assign<&ast::Holder::item>(
      create<ast::Item>(assign<&ast::Item::text>(lit<"a">) +
                        nest<&ast::Item::previous>(lit<"!">)))))> {};
  using entry = Root;
};
}  // namespace refuse
