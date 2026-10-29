// A directive over a sequence that builds a node, into a member that takes a
// value: the node would be held by nothing.
// expect LR: stands over a shape that builds a node, and the member takes a value
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Item : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>))> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(
                    lit<"("> + ref<Item> + lit<")">))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
