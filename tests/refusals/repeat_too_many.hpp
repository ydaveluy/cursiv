// A counted repetition has a position for every turn it may take: 70 000 turns
// are more than a program holds, and the lowering says so before it writes one.
// expect LR: refused: the grammar has an alternative longer than 65 535 items once expanded
// expect LL: refused: rule .Root. is too large for the LL parser once expanded
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Root : rule<create<ast::Node>(repeat<2, 70000>(lit<"x">))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
