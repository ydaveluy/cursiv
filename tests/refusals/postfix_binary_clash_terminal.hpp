// A postfix operator that is a terminal reading a choice of words, one of them
// also binary: the words are compared one by one.
// expect LR LL: refused .postfixBinaryClash..*rule .Sum. uses .[+]. as a postfix operator on level 1 and as a binary operator on level 2
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Expr : cursiv::Extends<cursiv::AstNode> {};
struct Num : cursiv::Extends<Expr> {
  string_view text;
};
struct Bin : cursiv::Extends<Expr> {
  Expr* left = nullptr;
  string_view op;
  Expr* right = nullptr;
};
struct Post : cursiv::Extends<Expr> {
  Expr* operand = nullptr;
  string_view op;
};
}  // namespace ast
struct G : Base {
  struct NUM : terminal<some(d)> {};
  struct STEP : terminal<lit<"+"> | lit<"-">> {};
  struct Primary : rule<create<ast::Num>(assign<&ast::Num::text>(ref<NUM>))> {};
  struct Sum : infix<Primary, ast::Bin,
                     levels(postfix<ast::Post>(ref<STEP>), left(lit<"+">))> {};
  using entry = Sum;
  using ignored = WS;
};

}  // namespace refuse
