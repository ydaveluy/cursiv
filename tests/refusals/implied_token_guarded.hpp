// An implied token is an unguarded literal: a keyword checks the byte after
// it, which a token laid with no width has none to check.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>) +
                                       implied(kw<"end">, eof))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
