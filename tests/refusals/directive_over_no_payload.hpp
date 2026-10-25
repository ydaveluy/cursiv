// A directive over a quantifier alone, with no node in it: nothing is stored,
// so the member it names would be written from nothing.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Root
      : rule<create<ast::Node>(assign<&ast::Node::name>(option(ref<ID>)))> {};
  using entry = Root;
  using ignore = WS;
};

}  // namespace refuse
