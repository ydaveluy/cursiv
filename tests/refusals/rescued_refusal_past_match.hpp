// A match that ends exactly where a word it refuses past a head of any width
// ends: whether the refusal fires is the byte behind the match, and a
// keyword's rescue asks that that byte BE a word byte -- here `_`, which the
// body does not read on over.
// expect LR: refused: terminal .W. may end a match exactly where a word its guard excludes ends, after the start of the terminal, and ._. after that word lets the guard pass
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct W : terminal<some(cr<"x">) + !kw<"if"> + cr<"a-z"> +
                      some(cr<"a-zA-Z0-9">)> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<W>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
