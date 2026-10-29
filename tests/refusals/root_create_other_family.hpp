// A root create over alternatives that create their own node, one of them a
// class outside the family the member holding the rule takes.
// expect LR: this member holds a node of its own type: the payload must build a node whose type derives
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Expr : cursiv::Extends<cursiv::AstNode> {};
struct Name : cursiv::Extends<Expr> {
  string_view name;
};
struct Label : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct Root : cursiv::Extends<cursiv::AstNode> {
  Expr* e = nullptr;
};
}  // namespace ast

struct G : Base {
  struct Item
      : rule<create<ast::Expr>(
            create<ast::Name>(lit<"n"> + assign<&ast::Name::name>(ref<ID>)) |
            create<ast::Label>(lit<"l"> + assign<&ast::Label::name>(ref<ID>)))> {
  };
  struct Root
      : rule<create<ast::Root>(assign<&ast::Root::e>(ref<Item>) + lit<";">)> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
