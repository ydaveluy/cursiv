// A flag writes a scalar member, just like assign.
// expect LR at compile: WrittenAgain<.*refuse::G::Root.*Item::on
#pragma once
#include "refusals/head.hpp"
namespace refuse {
namespace ast {
struct Item : cursiv::Extends<cursiv::AstNode> {
  boolean on = false;
};
}  // namespace ast
struct G : Base {
  struct Root : rule<flag<&ast::Item::on>(lit<"!">) +
                     assign<&ast::Item::on>(as<false>(lit<"?">))> {};
  using entry = Root;
};
}  // namespace refuse
