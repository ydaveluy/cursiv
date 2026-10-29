// Two alternatives that part only past their first token, one a section:
// the point behind that token reads the embedded grammar's first token and
// the caller's at once.
// expect LR LL: refused: the grammar reaches one point inside a .section<>. and outside it at once
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct Inner : cursiv::GrammarSpec {
  struct SP : terminal<some(cr<R"( \n)">)> {};
  struct NUM : terminal<some(d)> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<NUM>))> {};
  using entry = Root;
  using ignored = SP;
};

struct G : Base {
  struct Root
      : rule<section<Inner>(lit<"<">, lit<">">) |
             create<ast::Node>(lit<"<"> + assign<&ast::Node::name>(ref<ID>) +
                               lit<">">)> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
