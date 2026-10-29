// A negated lookahead over a rule that builds a node: it is parsed, and a
// condition only reads.
// expect LR: [Aa] condition cannot test a rule that builds a node
#pragma once

#include "refusals/head.hpp"

namespace refuse {
struct G : Base {
  struct Name : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>))> {};
  struct Root : rule<create<ast::Node>(!ref<Name> +
                                       assign<&ast::Node::name>(ref<ID>))> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace refuse
