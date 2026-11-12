// A choice whose other branch does not nest: the next turn writes `text` on
// the same node.
// expect LR at compile: WrittenEachTurn<.*refuse::G::Root.*refuse::ast::Item::text
#pragma once
#include "refusals/head.hpp"
namespace refuse {
namespace ast {
struct Item : cursiv::Extends<cursiv::AstNode> {
  string_view text;
  Item* previous = nullptr;
};
}  // namespace ast
struct G : Base {
  struct Root : rule<create<ast::Item>(some(
      assign<&ast::Item::text>(lit<"a">) +
      (nest<&ast::Item::previous>(lit<"!">) | lit<"?">)))> {};
  using entry = Root;
};
}  // namespace refuse
