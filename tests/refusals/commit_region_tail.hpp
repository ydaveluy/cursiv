// A commit that may leave a tight region's last item out: the point after it
// is read inside the region and outside it, as for an option.
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
