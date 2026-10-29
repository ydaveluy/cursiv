// A precedence table listing an operator where a level stands: `levels(...)`
// takes `left(op)`, `right(op)` or `nonassoc(op)`, which say how it groups.
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
  struct Sum : infix<Primary, ast::Bin, levels(left(lit<"*">), lit<"+">)> {};
  using entry = Sum;
  using ignored = WS;
};

}  // namespace refuse
