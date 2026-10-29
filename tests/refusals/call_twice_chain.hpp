// A precedence chain spelled without `infix`: each level calls the next one
// twice, and the parentheses close the cycle back to the top.
// expect LR: this alternative can create two nodes
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Item : cursiv::Extends<cursiv::AstNode> {};
struct Atom : cursiv::Extends<Item> {
  string_view name;
};
struct List : cursiv::Extends<cursiv::AstNode> {
  vector<Item*> items;
};
}  // namespace ast
struct G : Base {
  struct Sum;
  struct Product;
  struct Primary;
  struct Root : rule<create<ast::List>(
                    many(append<&ast::List::items>(ref<Sum>) + lit<";">))> {};
  struct Sum : rule<ref<Product> + many(lit<"+"> + ref<Product>)> {};
  struct Product : rule<ref<Primary> + many(lit<"*"> + ref<Primary>)> {};
  struct Primary : rule<create<ast::Atom>(assign<&ast::Atom::name>(ref<ID>)) |
                        (lit<"("> + ref<Sum> + lit<")">)> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
