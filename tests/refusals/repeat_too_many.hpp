// A counted repetition has a position for every turn it may take: 70 000 turns
// are more than a program holds, and the lowering says so before it writes one.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Root : rule<create<ast::Node>(repeat<2, 70000>(lit<"x">))> {};
  using entry = Root;
  using ignore = WS;
};

}  // namespace refuse
