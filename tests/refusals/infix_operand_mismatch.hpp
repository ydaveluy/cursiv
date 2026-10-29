// An infix rule synthesizes its body, so its operands carry no directive to
// refuse them: `left` and `right` hold a class the primary does not build.
// expect LR: this member holds a node of its own type
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Other : cursiv::Extends<cursiv::AstNode> {};
struct Bin : cursiv::Extends<cursiv::AstNode> {
  Other* left = nullptr;
  string_view op;
  Other* right = nullptr;
};
}  // namespace ast

struct G : Base {
  struct NUM : terminal<some(d)> {};
  // The climb builds `Bin`, never `Other`: no reading fills the two operands.
  struct Primary : rule<create<ast::Bin>(assign<&ast::Bin::op>(ref<NUM>))> {};
  struct Sum : infix<Primary, ast::Bin, (left(lit<"+">))> {};
  using entry = Sum;
  using ignored = WS;
};

}  // namespace refuse
