// An implied terminal that reads one or more line ends: an implied token needs
// one fixed spelling to insert.
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
