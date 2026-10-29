// A condition made optional conditions nothing.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Root : rule<create<ast::Node>(lit<"go"> + option(!lit<"x">) +
                                       assign<&ast::Node::name>(ref<ID>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
