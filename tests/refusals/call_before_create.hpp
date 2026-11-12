// An unassigned call to an AST rule, then a `create`: the alternative builds
// two nodes.
// expect LR at compile: this alternative can create two nodes
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Item : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct Holder : cursiv::Extends<cursiv::AstNode> {};
}  // namespace ast
struct G : Base {
  struct ItemRule
      : rule<create<ast::Item>(lit<"@"> + assign<&ast::Item::name>(ref<ID>))> {
  };
  struct Root : rule<ref<ItemRule> + create<ast::Holder>(lit<"|">)> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
