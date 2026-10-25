// Two alternatives spelling the same symbols and writing different members:
// no lookahead tells them apart.
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
  using ignore = WS;
};

}  // namespace refuse
