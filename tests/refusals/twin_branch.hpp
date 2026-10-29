// Two alternatives reading the same tokens and writing different members: the
// second is never chosen, so the generator names it and drops it.
// expect LR LL: warning: rule .Root.: branch 2 of 2 .b=ID. reads the same tokens as branch 1, so it is never chosen and is dropped
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace two {
struct Node : cursiv::Extends<cursiv::AstNode> {
  cursiv::AstNode::string_view a, b;
};
}  // namespace two
struct G : Base {
  struct Root : rule<create<two::Node>(assign<&two::Node::a>(ref<ID>) |
                                       assign<&two::Node::b>(ref<ID>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
