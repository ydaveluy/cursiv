#pragma once

// An operand two rules derive, each on a state of its own until the rule
// above reduces: `a ?? b` reads only through `Coalesce`, `a || b` only through
// `Or`, and a bare operand through both. The two readings of one operand never
// take a different token, so the look-ahead must find them one.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tfold {

namespace ast {
struct Expr : cursiv::Extends<cursiv::AstNode> {};
struct Name : cursiv::Extends<Expr> {
  string_view name;
};
struct Bin : cursiv::Extends<Expr> {
  Expr* left = nullptr;
  string_view op;
  Expr* right = nullptr;
};
struct Cond : cursiv::Extends<Expr> {
  Expr* test = nullptr;
  Expr* yes = nullptr;
  Expr* no = nullptr;
};
}  // namespace ast

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Primary : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>))> {
  };
  struct Or : infix<Primary, &ast::Bin::left, &ast::Bin::op, &ast::Bin::right,
                    left(lit<"||">)> {};
  struct Coalesce : infix<Primary, &ast::Bin::left, &ast::Bin::op,
                          &ast::Bin::right, left(lit<"??">)> {};
  struct Binary : rule<ref<Or> | ref<Coalesce>> {};
  struct Ternary
      : rule<create<ast::Cond>(assign<&ast::Cond::test>(ref<Binary>) +
                               lit<"?", !cr<"?">> +
                               assign<&ast::Cond::yes>(ref<Ternary>) +
                               lit<":"> +
                               assign<&ast::Cond::no>(ref<Ternary>))  //
             | ref<Binary>> {};
  using entry = Ternary;
  using ignore = WS;
};

}  // namespace tfold
