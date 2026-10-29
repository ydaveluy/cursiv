// A counted tail of nest turns: the LL descent spells each turn inside the one
// before it, so its source nests one block deeper per turn.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Expr : cursiv::Extends<cursiv::AstNode> {};
struct Name : cursiv::Extends<Expr> {
  string_view name;
};
struct Index : cursiv::Extends<Expr> {
  Expr* array = nullptr;
};
}  // namespace ast
struct G : Base {
  struct Atom : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>))> {};
  struct Root : rule<ref<Atom> + repeat<0, 2000>(nest<&ast::Index::array>(
                                     lit<"["> + lit<"]">))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
