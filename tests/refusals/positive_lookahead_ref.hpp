// A positive lookahead over a rule that builds a node: it is parsed, and a
// condition only reads.
// expect LR: A rule that builds a node is parsed, not read: no condition asks one
#pragma once

#include "refusals/head.hpp"

namespace refuse {
struct G : Base {
  struct Name : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>))> {};
  struct Root : rule<create<ast::Node>(
      (&ref<Name>) + assign<&ast::Node::name>(ref<ID>))> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace refuse
