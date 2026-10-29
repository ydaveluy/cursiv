// A root create followed by a second create, of a class outside the family the
// member holding the rule takes.
// expect LR: this member holds a node of its own type: the payload must build a node whose type derives
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Expr : cursiv::Extends<cursiv::AstNode> {};
struct Label : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct Root : cursiv::Extends<cursiv::AstNode> {
  Expr* e = nullptr;
};
}  // namespace ast

struct G : Base {
  struct Item : rule<create<ast::Expr> +
                     create<ast::Label>(assign<&ast::Label::name>(ref<ID>))> {};
  struct Root
      : rule<create<ast::Root>(assign<&ast::Root::e>(ref<Item>) + lit<";">)> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
