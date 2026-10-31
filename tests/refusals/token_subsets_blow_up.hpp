// A terminal whose subset construction yields about 2^21 states: the lexer
// refuses as soon as the subsets pass eight times the limit, before a row or a
// splitting array is held for the millions it would otherwise number.
// expect LR LL: has terminals too complex for the generated lexer
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Blob : terminal<many(cr<"ab">) + lit<"a"> + repeat<20>(cr<"ab">)> {};
  struct Root : rule<create<ast::Node>(ref<Blob>)> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
