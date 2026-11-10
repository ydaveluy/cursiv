// Two alternatives that differ only after their first token, one of them a
// section: after `<`, the next token belongs to the embedded grammar and to
// the caller at once.
// expect LR LL: refused: the grammar reaches the same token both inside a .section<>. and outside it
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
