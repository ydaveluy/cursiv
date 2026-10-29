// Two alternatives that share their first tokens, one of them inside a
// region: the state holding both cannot say whether the space is legal.
// expect LR: refused: the grammar reaches one point through a .* region and outside it at once
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Root
      : rule<create<ast::Node>(
            tight(lit<"x"> + lit<"a"> + lit<"b">) |
            (lit<"x"> + lit<"a"> + lit<"c">))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
