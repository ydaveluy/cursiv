// A commit that may leave a tight region's last item out: the point after it
// is read inside the region and outside it, as for an option.
// expect LR LL: refused: the grammar reaches the same token both inside a .* region and outside it
#pragma once

#include "refusals/head.hpp"

namespace refuse {
struct G : Base {
  struct Root
      : rule<create<ast::Node>(
            lit<"x"> + tight(lit<"c"> + option(commit(lit<"a">, lit<"b">))) +
            lit<"y">)> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace refuse
