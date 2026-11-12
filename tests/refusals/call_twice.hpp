// Two unassigned calls to an AST rule on one path: the second node would
// replace the first.
// expect LR at compile: this alternative can create two nodes
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Item : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
}  // namespace ast
struct G : Base {
  struct ItemRule
      : rule<create<ast::Item>(lit<"@"> + assign<&ast::Item::name>(ref<ID>))> {
  };
  struct Root : rule<ref<ItemRule> + lit<"|"> + ref<ItemRule>> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
