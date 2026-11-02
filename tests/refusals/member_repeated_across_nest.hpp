// The suffix of one iteration and the prefix of the next share the wrapper.
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
