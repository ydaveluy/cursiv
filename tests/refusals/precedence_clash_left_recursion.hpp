// One operator on two levels of a table that stands in a left recursion: each
// level is a rule of its own, yet `1 - 2 - 3` reads two ways.
// expect LR LL: refused .precedenceClash..*rule .Sum. names the same operator text on two levels
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
struct Bang : cursiv::Extends<Expr> {
  Expr* value = nullptr;
};
}  // namespace ast
struct G : Base {
  struct NUM : terminal<some(d)> {};
  struct Sum;
  struct Primary : rule<create<ast::Num>(assign<&ast::Num::text>(ref<NUM>)) |
                        create<ast::Bang>(assign<&ast::Bang::value>(ref<Sum>) +
                                          lit<"!">)> {};
  struct Sum
      : infix<Primary, ast::Bin, levels(left(lit<"-">), left(lit<"-">))> {};
  using entry = Sum;
  using ignored = WS;
};

}  // namespace refuse
