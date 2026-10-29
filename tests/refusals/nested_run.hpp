// A run written inside a run: the generation goes on, and names the
// repetition.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct SEMI : terminal<lit<";">> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>) +
                                       many(some(ref<SEMI>)))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
