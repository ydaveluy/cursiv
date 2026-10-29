// A rule read outside every region and inside one: the reading outside names
// the skipped terminal where the lexer never hands it over.
// expect LR: refused .skippedTerminal..*rule .Pair. names a terminal its reading skips as trivia
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Pair : rule<ref<ID> + ref<WS> + ref<ID>> {};
  struct Root : rule<create<ast::Node>(
                    (lit<"p"> + ref<Pair>) |
                    tight(lit<"["> + ref<Pair> + lit<"]">))> {
  };
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
