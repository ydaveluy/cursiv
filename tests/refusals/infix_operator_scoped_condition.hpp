// An operator made of a region holding a condition, then a rule that builds a
// node: the operator still names that rule, which gives no text.
// expect LR LL: refused .infixOperatorNotText..*rule .Sum. names as an operator something other than one token or a rule that gives a value
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
  struct Sum
      : infix<Primary, ast::Bin,
              (left(skip(ignore<WS>, hide<>, !gap::line) + ref<OpNode>))> {};
  using entry = Sum;
  using ignored = WS;
};

}  // namespace refuse
