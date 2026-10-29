// A gap condition over a run the gap matcher does not read.
// expect LR LL: refused: the grammar uses an expression in .gap::has. or .gap::is. that they cannot test
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Root : rule<create<ast::Node>(gap::has(through(lit<"/*">, lit<"*/">)) +
                                       assign<&ast::Node::name>(ref<ID>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
