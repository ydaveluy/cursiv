// A helper rule that repeats a node: each turn is a head of its own, and the
// rule hands over only the first.
// expect LR: this alternative can create twice.*each turn of a repetition
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Bag : cursiv::Extends<cursiv::AstNode> {
  vector<Node*> items;
};
}  // namespace ast
struct G : Base {
  struct Item : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>))> {};
  struct Items : rule<some(ref<Item>)> {};
  struct Root : rule<create<ast::Bag>(append<&ast::Bag::items>(ref<Items>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
