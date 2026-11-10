// A loop whose item starts with a guard: the guard does not stop the body from
// matching the empty string.
// expect LR: terminal .NOISE. matches the empty string
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct NOISE : terminal<many(!lit<"ab"> + any)> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>)) +
                     many(ref<NOISE>)> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
