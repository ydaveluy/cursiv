// A rule adopts a node of a base class and writes a member only a subclass
// declares: a document whose node is not that subclass has nowhere to write.
// expect LR: this directive writes a member that the current node may not have
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Expr : cursiv::Extends<cursiv::AstNode> {};
struct Name : cursiv::Extends<Expr> {
  string_view name;
};
struct Typed : cursiv::Extends<Name> {
  string_view ty;
};
struct Root : cursiv::Extends<cursiv::AstNode> {
  Expr* e = nullptr;
};
}  // namespace ast

struct G : Base {
  struct NameR : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>))> {};
  struct TypedR
      : rule<create<ast::Typed>(lit<"@"> + assign<&ast::Name::name>(ref<ID>))> {
  };
  struct Prim : rule<ref<NameR> | ref<TypedR>> {};
  struct R : rule<ref<Prim> + lit<":"> + assign<&ast::Typed::ty>(ref<ID>)> {};
  struct Root
      : rule<create<ast::Root>(assign<&ast::Root::e>(ref<R>) + lit<";">)> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
