// A rule that calls itself behind a rule that may match nothing: the descent
// cannot fold the cycle into a loop, and would never return.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Sign : rule<option(lit<"-">)> {};
  struct Path : rule<(ref<Sign> + ref<Path> + lit<"."> + ref<ID>) | ref<ID>> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<Path>))> {};
  using entry = Root;
  using ignore = WS;
};

}  // namespace refuse
