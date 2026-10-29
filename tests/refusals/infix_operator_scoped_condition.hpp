// A condition inside a region, in front of a rule that builds a node named as
// an operator: the region leaves the operator the rule it names.
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
