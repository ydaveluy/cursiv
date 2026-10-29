// An echo needs its mark on every path: a loop that may not turn sets none.
// expect LR: refused: terminal .TAG. uses .same<>. where a .mark<>. with its tag is not set on every path before it
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct Fence {};

struct G : Base {
  struct TAG : terminal<many(mark<Fence>(some(lit<"#">)) + lit<":">) +
                        same<Fence> + lit<"!">> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<TAG>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
