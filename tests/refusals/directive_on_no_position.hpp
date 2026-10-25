// A directive over the end assertion: `eof` consumes nothing, so there is no
// record for the write to ride and the member would never be written.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Doc : cursiv::Extends<cursiv::AstNode> {
  string_view n;
  boolean last;
};
}  // namespace ast

struct G : Base {
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::n>(ref<ID>) +
                                      flag<&ast::Doc::last>(eof))> {};
  using entry = Root;
  using ignore = WS;
};

}  // namespace refuse
