// A flag whose operand nests sets its member on the wrapper, which is not an
// Item.
// expect LR: this directive writes a member of a class the node in hand may not be
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Item : cursiv::Extends<Node> {
  boolean on;
};
struct Wrapper : cursiv::Extends<Node> {
  Node* inner = nullptr;
};
}  // namespace ast

struct G : Base {
  struct Root
      : rule<create<ast::Item> + lit<"#"> + assign<&ast::Item::name>(ref<ID>) +
             flag<&ast::Item::on>(
                 nest<ast::Wrapper, &ast::Wrapper::inner>(lit<".">))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
