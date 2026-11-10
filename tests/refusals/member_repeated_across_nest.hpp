// The write after the nest and the next turn's write before it go to the
// same wrapper.
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
      nest<&ast::Item::previous>(lit<"!">) +
      assign<&ast::Item::text>(lit<"b">)))> {};
  using entry = Root;
};
}  // namespace refuse
