// A second call on one path returns a node the first one's replaces.
// expect LR: this alternative can create twice
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
