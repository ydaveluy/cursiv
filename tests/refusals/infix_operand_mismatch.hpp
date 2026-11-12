// An infix rule writes `left` and `right` itself: here they hold `Other`, a
// class that neither the primary nor the infix rule builds.
// expect LR at compile: this member holds a node of its own type
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
  struct Primary : rule<create<ast::Bin>(assign<&ast::Bin::op>(ref<NUM>))> {};
  struct Sum : infix<Primary, ast::Bin, (left(lit<"+">))> {};
  using entry = Sum;
  using ignored = WS;
};

}  // namespace refuse
