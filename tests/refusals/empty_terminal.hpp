// A terminal whose body may match nothing: the lexer never produces an empty
// token, so a rule could wait for one forever.
// expect LR LL: terminal .MaybeNum. matches the empty string
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct MaybeNum : terminal<many(d)> {};
  struct Root : rule<create<ast::Node>(some(ref<MaybeNum> + lit<";">))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
