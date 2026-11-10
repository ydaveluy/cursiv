// A name whose keyword guard excludes `if`, then may read a symbol outside the
// word boundary: `if` before such a symbol is excluded, yet the name could
// read on over it.
// expect LR: refused: terminal .NAME. excludes a word with a keyword guard, then may read after it a character outside .word_boundary.
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
