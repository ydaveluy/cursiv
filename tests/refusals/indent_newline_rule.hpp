// The line's end named by a rule: nothing would ever mark it.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct NEWLINE : terminal<eol> {};
  struct Line : rule<ref<NEWLINE>> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>) +
                                       ref<Line>)> {};
  using entry = Root;
  using ignore = WS;
  using indent_newline = Line;
};

}  // namespace refuse
