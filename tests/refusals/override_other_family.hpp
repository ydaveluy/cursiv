// An override that adds an alternative building a class outside the family of
// the rule it stands over: every caller of the base reads an `Item` and would
// find another class there.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Item : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct Other : cursiv::Extends<cursiv::AstNode> {
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
  using ignore = WS;
};

struct G : cursiv::extends<Parent> {
  template <class>
  struct override;

  template <class T>
    requires std::same_as<T, Parent::ItemRule>
  struct override<T>
      : rule<create<ast::Other>(kw<"other"> +
                                assign<&ast::Other::name>(ref<ID>)) |
             super> {};
};

}  // namespace refuse
