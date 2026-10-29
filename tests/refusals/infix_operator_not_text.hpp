// A rule that builds a node named as an operator: an operator hands over text.
// expect LR LL: refused .infixOperatorNotText..*rule .Sum. names as an operator a rule that builds a node
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
  struct OpNode : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>))> {};
  struct Primary : rule<create<ast::Bin>(assign<&ast::Bin::op>(ref<NUM>))> {};
  struct Sum : infix<Primary, ast::Bin, (left(ref<OpNode>))> {};
  using entry = Sum;
  using ignored = WS;
};

}  // namespace refuse
