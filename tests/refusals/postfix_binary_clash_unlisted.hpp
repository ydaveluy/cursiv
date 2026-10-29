// A postfix operator spelled through a repetition, in a table with a binary
// operator: the strings it reads cannot be listed, so a clash with the binary
// operator cannot be ruled out.
// expect LR LL: refused .postfixBinaryClash..*rule .Sum. has two operators whose texts cannot be compared, the postfix operator on level 1 and the binary operator on level 2
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
  struct Bangs : rule<some(lit<"!">)> {};
  struct Primary : rule<create<ast::Num>(assign<&ast::Num::text>(ref<NUM>))> {};
  struct Sum : infix<Primary, ast::Bin,
                     levels(postfix<ast::Post>(ref<Bangs>),
                            left(lit<"+"> | lit<"!">))> {};
  using entry = Sum;
  using ignored = WS;
};

}  // namespace refuse
