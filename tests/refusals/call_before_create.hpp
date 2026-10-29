// The same two heads in the other order: a create absorbs what stands before
// it, the call included.
// expect LR: this alternative can create two nodes
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
