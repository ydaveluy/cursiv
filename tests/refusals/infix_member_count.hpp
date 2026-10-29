// An infix rule naming one of its members: it names all three, or none to
// climb over `left`, `op` and `right`.
// expect LR: an infix rule names zero or three members
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Assign : cursiv::Extends<cursiv::AstNode> {
  Assign* target = nullptr;
  string_view op;
  Assign* value = nullptr;
};
}  // namespace ast
struct G : Base {
  struct NUM : terminal<some(d)> {};
  struct Primary
      : rule<create<ast::Assign>(assign<&ast::Assign::op>(ref<NUM>))> {};
  struct Set
      : infix<Primary, ast::Assign, right(lit<"=">), &ast::Assign::target> {};
  using entry = Set;
  using ignored = WS;
};

}  // namespace refuse
