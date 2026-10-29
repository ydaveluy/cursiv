// An implied token is a literal: a terminal that reads more than one has no
// spelling to lay.
// expect LR LL: implied.tok, cond.. lays a token of no width, so .tok. is an unguarded literal
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
