// An echo needs its mark on every path: one arm of the choice sets none.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct Fence {};

struct G : Base {
  struct TAG : terminal<((mark<Fence>(some(lit<"#">)) + lit<":">) |
                         lit<"none:">)+same<Fence> +
                        lit<"!">> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<TAG>))> {};
  using entry = Root;
  using ignore = WS;
};

}  // namespace refuse
