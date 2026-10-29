// An override that stops a node rule building its node: callers of the base
// would still read the rule as the node's type.
// expect LR: this override changes whether the rule builds a node
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Item : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Item*> items;
};
}  // namespace ast

struct Parent : Base {
  struct ItemRule : rule<kw<"item"> + assign<&ast::Item::name>(ref<ID>)> {};
  struct Top
      : rule<create<ast::Doc>(many(append<&ast::Doc::items>(ref<ItemRule>)))> {
  };
  using entry = Top;
  using ignored = WS;
};

struct G : cursiv::extends<Parent> {
  template <class>
  struct override;

  template <class T>
    requires std::same_as<T, Parent::ItemRule>
  struct override<T> : rule<ref<ID>> {};
};

}  // namespace refuse
