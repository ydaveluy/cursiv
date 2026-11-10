// A `create` and an unassigned call to an AST rule on one path: the node the
// call returns would be held by nothing.
// expect LR: this alternative can create two nodes
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Item : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct Holder : cursiv::Extends<cursiv::AstNode> {
  Item* first{};
};
}  // namespace ast
struct G : Base {
  struct ItemRule
      : rule<create<ast::Item>(lit<"@"> + assign<&ast::Item::name>(ref<ID>))> {
  };
  struct Root
      : rule<create<ast::Holder>(assign<&ast::Holder::first>(ref<ItemRule>) +
                                 lit<"|"> + ref<ItemRule> + eof)> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
