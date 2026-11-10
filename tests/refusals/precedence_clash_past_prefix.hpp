// One operator on two binary levels with a prefix level between them: each
// side of the prefix level is its own table, yet `1 - 2 - 3` reads two ways.
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
struct Not : cursiv::Extends<Expr> {
  string_view op;
  Expr* operand = nullptr;
};
}  // namespace ast
struct G : Base {
  struct NUM : terminal<some(d)> {};
  struct Primary : rule<create<ast::Num>(assign<&ast::Num::text>(ref<NUM>))> {};
  struct Sum : infix<Primary, ast::Bin,
                     levels(left(lit<"-">), prefix<ast::Not>(lit<"!">),
                            left(lit<"-">))> {};
  using entry = Sum;
  using ignored = WS;
};

}  // namespace refuse
