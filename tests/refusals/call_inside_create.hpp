// A creation and a call on one path are two heads: the node the call returns
// would be held by nothing.
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
