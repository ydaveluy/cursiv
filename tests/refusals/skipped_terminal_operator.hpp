// An operator rule that reads a terminal the grammar skips: LL inlines the
// operator's rule, so its error names the infix rule `Sum`.
// expect LR: refused .skippedTerminal..*rule .Plus. names a terminal that is skipped as trivia at that point
// expect LL: refused .skippedTerminal..*rule .Sum. names a terminal that is skipped as trivia at that point
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
  struct Sum : infix<Primary, ast::Bin, (left(ref<Plus>))> {};
  using entry = Sum;
  using ignored = WS;
};

}  // namespace refuse
