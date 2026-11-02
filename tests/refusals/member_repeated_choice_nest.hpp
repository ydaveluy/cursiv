// A non-nesting branch leaves the current node for the following iteration.
// expect LR: WrittenEachTurn<.*refuse::G::Root.*refuse::ast::Item::text
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
