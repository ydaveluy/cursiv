// A terminal whose body may match nothing: the lexer reads no token of no
// width, so the parser owes one it can never be given.
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
