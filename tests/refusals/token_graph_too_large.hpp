// A count no token graph holds: refused before it fills the memory.
// expect LR: refused: the grammar has terminals too complex for the generated lexer
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct RUN : terminal<repeat<300000000>(cr<"a">)> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<RUN>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
