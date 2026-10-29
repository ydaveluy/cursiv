// An implied token is a literal: a terminal that reads more than one has no
// spelling to lay.
// expect LR LL: in .implied.tok, cond.., .tok. must be a literal with no guard, or a terminal whose body is one
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct NEWLINE : terminal<eol + many(eol)> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>) +
                                       implied(ref<NEWLINE>, eof))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
