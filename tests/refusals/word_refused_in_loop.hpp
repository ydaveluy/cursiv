// A repetition that comes back to its refusal without reading a byte: the
// token graph cannot spell the turn that follows the refusal.
// expect LR: refused: terminal .STARS. refuses a word inside a repetition that may come back to a refusal without reading a byte
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct STARS : terminal<lit<"#"> + many(lit<"*"> | !lit<"/">)> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>) +
                                       ref<STARS>)> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
