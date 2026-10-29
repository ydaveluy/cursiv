// A grammar named with `ref<>`: it has no tokens around it, so nothing says
// where its text ends.
// expect LR: `ref<G>` names a grammar: read it with `section<G>
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
  struct Root : rule<lit<"<"> + ref<Inner> + lit<">">> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
