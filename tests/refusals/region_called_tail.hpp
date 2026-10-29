// A region ends on a call whose rule may read nothing: the point after the
// region's first token reads the rule's token inside the region and the
// caller's outside it.
// expect LR LL: refused: the grammar reaches one point through a .* region and outside it at once
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Tail : rule<option(lit<"x">)> {};
  struct Root : rule<create<ast::Node>(lit<"p"> + tight(lit<"a"> + ref<Tail>) +
                                       lit<"c">)> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
