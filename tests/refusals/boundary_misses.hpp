// A name an escape may begin, reading one on past a word it refuses, under a
// word boundary without the escape's backslash: `inA` would be read as
// `in` and a second name.
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
