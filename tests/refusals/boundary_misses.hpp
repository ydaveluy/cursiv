// A name that excludes the keyword `in` and may go on with an escape, whose
// backslash is outside the word boundary: `in` then an escape would be read
// as `in` and a second name.
// expect LR: refused: terminal .NAME. excludes ."in". at its start, and can read .\\. at the start of a word and again after ."in".
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  static constexpr auto Esc = lit<"\\u"> + repeat<4>(cr<"0-9a-f">);
  struct NAME
      : terminal<!kw<"in"> + (cr<"a-z"> | Esc) + many(cr<"a-z"> | Esc)> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<NAME>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
