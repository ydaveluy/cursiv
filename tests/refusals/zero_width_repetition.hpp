// A repetition whose item holds no position: it matches nothing at every turn,
// so the loop has no fixpoint either -- and, naming no symbol, it lowers as
// the one the default names.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Root : rule<create<ast::Node>(
                    assign<&ast::Node::name>(ref<ID>) + many(eof))> {};
  using entry = Root;
  using ignore = WS;
};

}  // namespace refuse
