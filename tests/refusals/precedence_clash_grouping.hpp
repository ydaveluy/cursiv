// One operator grouping left in one table and right in another: nothing tells
// the two readings apart.
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
  struct Inner : infix<Primary, &ast::Bin::left, &ast::Bin::op,
                       &ast::Bin::right, left(lit<"^">)> {};
  struct Outer : infix<Inner, &ast::Bin::left, &ast::Bin::op, &ast::Bin::right,
                       right(lit<"^">)> {};
  using entry = Outer;
  using ignore = WS;
};

}  // namespace refuse
