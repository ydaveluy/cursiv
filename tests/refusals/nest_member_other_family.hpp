// A nest buries the node in hand in its member, and the head in front of it
// builds a class that member does not hold.
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
  Item* left = nullptr;
  Item* right = nullptr;
};
struct Root : cursiv::Extends<cursiv::AstNode> {
  Expr* e = nullptr;
};
}  // namespace ast

struct G : Base {
  struct ItemR : rule<create<ast::Item>(assign<&ast::Item::name>(ref<ID>))> {};
  struct PairR
      : rule<create<ast::Other>(assign<&ast::Other::name>(ref<ID>)) +
             option(nest<ast::Pair, &ast::Pair::left>(
                 lit<":"> + assign<&ast::Pair::right>(ref<ItemR>)))> {};
  struct Root
      : rule<create<ast::Root>(assign<&ast::Root::e>(ref<PairR>) + lit<";">)> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
