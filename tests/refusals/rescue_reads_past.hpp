// A name reading on past a word it refuses over symbols its word boundary
// does not hold: `if` before one is refused, yet the token graph reads the
// symbol's bytes on as the name.
// expect LR: refused: terminal .NAME. reads on past a word it refuses over a character its word boundary does not hold
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  static constexpr auto word_boundary = w | uc::L;
  struct NAME : terminal<some(lit<"$">) + !kw<"if"> + cr<"a-z"> + cr<"a-z"> +
                         many(w | uc::L | uc::So)> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<NAME>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
