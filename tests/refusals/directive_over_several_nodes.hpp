// A directive over a repetition: the member takes one node per write, and the
// repetition holds as many as the text spells.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct List : cursiv::Extends<cursiv::AstNode> {
  vector<Node*> items;
};
}  // namespace ast

struct G : Base {
  struct Item : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>))> {};
  struct Root
      : rule<create<ast::List>(
            lit<"("> +
            append<&ast::List::items>(many(ref<Item>, lit<",">)) +
            lit<")">)> {};
  using entry = Root;
  using ignore = WS;
};

}  // namespace refuse
