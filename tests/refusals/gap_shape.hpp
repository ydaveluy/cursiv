// A gap condition over a run the gap matcher does not read.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Root : rule<create<ast::Node>(gap::has(through(lit<"/*">, lit<"*/">)) +
                                       assign<&ast::Node::name>(ref<ID>))> {};
  using entry = Root;
  using ignore = WS;
};

}  // namespace refuse
