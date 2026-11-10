// A terminal whose lexer construction yields about 2^21 states: refused as
// soon as the count passes eight times the limit, before any table is built.
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
