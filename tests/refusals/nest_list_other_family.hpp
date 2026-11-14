// A nest adds the node in hand to its list member, and the head in front of
// it builds a class that list does not hold.
// expect LR at compile: this nest stores the current node in a member that cannot hold it
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Expr : cursiv::Extends<cursiv::AstNode> {};
struct Item : cursiv::Extends<Expr> {
  string_view name;
};
struct Other : cursiv::Extends<Expr> {
  string_view name;
};
struct Pair : cursiv::Extends<Expr> {
  vector<Item*> items;
  Item* right = nullptr;
};
struct Root : cursiv::Extends<cursiv::AstNode> {
  Expr* e = nullptr;
};
}  // namespace ast

struct G : Base {
  struct ItemR : rule<create<ast::Item>(assign<&ast::Item::name>(ref<ID>))> {};
  struct PairR : rule<create<ast::Other>(assign<&ast::Other::name>(ref<ID>)) +
                      option(nest<ast::Pair, &ast::Pair::items>(
                          lit<":"> + assign<&ast::Pair::right>(ref<ItemR>)))> {
  };
  struct Root
      : rule<create<ast::Root>(assign<&ast::Root::e>(ref<PairR>) + lit<";">)> {
  };
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
