// A rule that reads a skipped terminal, called outside any region and inside a
// `tight()` one: the call outside is refused.
// expect LR: refused .skippedTerminal..*rule .Pair. names a terminal that is skipped as trivia at that point
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
