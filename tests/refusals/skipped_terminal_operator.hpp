// An operator spelling a terminal the grammar skips: the descent lays the
// operator's rule in place, so the refusal names the table it stands in.
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
  struct Plus : rule<lit<"+"> + ref<WS>> {};
  struct Primary : rule<create<ast::Bin>(assign<&ast::Bin::op>(ref<NUM>))> {};
  struct Sum : infix<Primary, &ast::Bin::left, &ast::Bin::op, &ast::Bin::right,
                     (left(ref<Plus>))> {};
  using entry = Sum;
  using ignore = WS;
};

}  // namespace refuse
