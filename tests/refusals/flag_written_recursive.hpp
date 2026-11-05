// Recursive adoption keeps the same node: another flag is another write.
// expect LR: WrittenOverAdopted<.*refuse::G::Item.*Item::on
#pragma once
#include "refusals/head.hpp"
namespace refuse {
namespace ast {
struct Item : cursiv::Extends<cursiv::AstNode> {
  boolean on = false;
};
}  // namespace ast
struct G : Base {
  struct Item : rule<create<ast::Item>(lit<"!">) |
                     flag<&ast::Item::on>(lit<"("> + ref<Item> + lit<")">)> {};
  using entry = Item;
};
}  // namespace refuse
