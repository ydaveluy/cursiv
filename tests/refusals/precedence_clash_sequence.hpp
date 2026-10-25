// Two operators written as the same sequence, on two levels: nothing after
// `not` tells them apart.
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
  struct Sum
      : infix<Primary, &ast::Bin::left, &ast::Bin::op, &ast::Bin::right,
              (left(kw<"not"> + kw<"in">) >> left(kw<"not"> + kw<"in">))> {};
  using entry = Sum;
  using ignore = WS;
};

}  // namespace refuse
