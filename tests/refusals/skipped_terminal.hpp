// A rule naming a terminal the grammar skips: the lexer never hands it over
// where it reads, so no text can match it.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>) +
                                       ref<WS> + lit<";">)> {};
  using entry = Root;
  using ignore = WS;
};

}  // namespace refuse
