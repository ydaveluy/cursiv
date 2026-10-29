// A root create whose node a nest may wrap into a class outside the one the
// member holding the rule takes.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Expr : cursiv::Extends<cursiv::AstNode> {};
struct Name : cursiv::Extends<Expr> {
  string_view name;
};
struct Call : cursiv::Extends<Expr> {
  Expr* callee = nullptr;
};
struct Root : cursiv::Extends<cursiv::AstNode> {
  Name* n = nullptr;
};
}  // namespace ast

struct G : Base {
  struct Item
      : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>) +
                               option(nest<ast::Call, &ast::Call::callee>(
                                   lit<"("> + lit<")">)))> {};
  struct Root
      : rule<create<ast::Root>(assign<&ast::Root::n>(ref<Item>) + lit<";">)> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
