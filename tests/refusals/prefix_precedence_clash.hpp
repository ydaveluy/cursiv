// One operator on two prefix levels of a precedence table: an operand reads
// it on either level.
// expect LR: refused .precedenceClash..*rule .Sum. names the same operator text on two levels
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
struct Neg : cursiv::Extends<Expr> {
  string_view op;
  Expr* operand = nullptr;
};
}  // namespace ast
struct G : Base {
  struct NUM : terminal<some(d)> {};
  struct Primary : rule<create<ast::Num>(assign<&ast::Num::text>(ref<NUM>))> {};
  struct Sum : infix<Primary, ast::Bin,
                     levels(prefix<ast::Neg>(lit<"-">), left(lit<"*">),
                            prefix<ast::Neg>(lit<"-"> | lit<"~">))> {};
  using entry = Sum;
  using ignored = WS;
};

}  // namespace refuse
