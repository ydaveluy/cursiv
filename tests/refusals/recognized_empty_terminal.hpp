// A refused word in a loop is read by recognition, past the automaton's start:
// the body still matches the empty string.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct NOISE : terminal<many(!lit<"ab"> + any)> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>)) +
                     many(ref<NOISE>)> {};
  using entry = Root;
  using ignore = WS;
};

}  // namespace refuse
