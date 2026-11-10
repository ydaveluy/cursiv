// A counted repetition of 2 000 nests: LL writes each turn inside the one
// before it, one block deeper per turn.
// expect LL: refused: rule .Root. repeats a .nest. more than 64 times in a row
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
