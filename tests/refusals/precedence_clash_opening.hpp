// Two rules that spell `not in`, on two levels: nothing after `not` tells them
// apart.
// expect LR LL: refused .precedenceClash..*rule .Sum. names the same operator text on two levels
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
  struct NotIn : rule<kw<"not"> + kw<"in">> {};
  struct NotInToo : rule<kw<"not"> + kw<"in">> {};
  struct Primary : rule<create<ast::Bin>(assign<&ast::Bin::op>(ref<NUM>))> {};
  struct Sum : infix<Primary, ast::Bin,
                     levels(left(ref<NotIn>), left(ref<NotInToo>))> {};
  using entry = Sum;
  using ignored = WS;
};

}  // namespace refuse
