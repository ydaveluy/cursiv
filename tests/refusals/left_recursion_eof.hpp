// A rule that calls itself behind `eof`: the assertion reads nothing, so at
// the end of the text the descent would call itself for ever.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Tail : rule<lit<"."> | (eof + ref<Tail>)> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>) +
                                       ref<Tail>)> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
