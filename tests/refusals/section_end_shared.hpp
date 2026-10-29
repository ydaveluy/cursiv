// A section closed by a token its grammar reads itself: where the section
// ends would be the parse's to say, not the text's.
// expect LR LL: refused: the grammar closes a .section<>. on a token that the embedded grammar also reads
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct Inner : cursiv::GrammarSpec {
  struct SP : terminal<some(cr<R"( \n)">)> {};
  struct NUM : terminal<some(d)> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<NUM>) +
                                       option(lit<";">))> {};
  using entry = Root;
  using ignored = SP;
};

struct G : Base {
  struct Root : rule<section<Inner>(lit<"<">, lit<";">)> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
