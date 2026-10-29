// An unordered group of one item: nothing to read in another order.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Root : rule<create<ast::Node>(unordered(lit<"a">))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
