// An operator that is an unordered group, neither one token nor a rule: the
// text it reads has no single value.
// expect LR LL: refused .infixOperatorNotText..*rule .Sum. names as an operator something other than one token or a rule that gives a value
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Bin : cursiv::Extends<cursiv::AstNode> {
  Bin* left = nullptr;
  string_view op;
  Bin* right = nullptr;
};
}  // namespace ast
struct G : Base {
  struct NUM : terminal<some(d)> {};
  struct Primary : rule<create<ast::Bin>(assign<&ast::Bin::op>(ref<NUM>))> {};
  struct Sum : infix<Primary, ast::Bin,
                     (left(unordered(lit<"+">, option(lit<"-">))))> {};
  using entry = Sum;
  using ignored = WS;
};

}  // namespace refuse
