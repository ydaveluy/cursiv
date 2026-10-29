// A directive over a quantifier that builds a node, into a member that takes a
// value: the node would be held by nothing.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Item : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>))> {};
  struct Root
      : rule<create<ast::Node>(assign<&ast::Node::name>(option(ref<Item>)))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
