// A region ending on a call to a rule that may read only a condition: the
// condition looks past the region, so after `a` the same token is read inside
// the region and outside it.
// expect LR LL: refused: the grammar reaches the same token both inside a .* region and outside it
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Tail : rule<option(lit<"x">) + !lit<"z">> {};
  struct Root : rule<create<ast::Node>(lit<"p"> + tight(lit<"a"> + ref<Tail>) +
                                       lit<"c">)> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
