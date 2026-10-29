// An implied token is an unguarded literal: a keyword checks the byte after
// it, which a token laid with no width has none to check.
// expect LR LL: in .implied.tok, cond.., .tok. must be a literal with no guard, or a terminal whose body is one
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
