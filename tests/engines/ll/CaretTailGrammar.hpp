#pragma once

// A block comment only its own delimiter closes, opened by an operator in
// front of another: whether `/` may stand before a `*` is read as far as the
// comment it would open runs.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tlltail {

namespace ast {
struct Expr : cursiv::Extends<cursiv::AstNode> {};
struct Name : cursiv::Extends<Expr> {
  string_view id;
};
struct Bin : cursiv::Extends<Expr> {
  Expr* left{};
  string_view op;
  Expr* right{};
};
}  // namespace ast

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct BLOCK : terminal<through(lit<"/*">, lit<"*/">)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Primary : rule<create<ast::Name>(assign<&ast::Name::id>(ref<ID>))> {};
  struct Expression : infix<Primary, ast::Bin, left(lit<"*"> | lit<"/">)> {};
  using entry = Expression;
  using ignored = WS;
  using hidden = list<BLOCK>;
};
struct Ll : Base {};
struct Lr : Base {};

}  // namespace tlltail
