// A terminal reading a data rule: `Dotted` skips the blanks between its parts
// where a rule reads it, and a terminal has no skipper to read it with.
// expect LR: refused: the grammar reads the rule .(Pair|Dotted).: a rule is read with the skipper between its parts
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Dotted : rule<ref<ID> + lit<"."> + ref<ID>> {};
  struct PATH : terminal<lit<"@"> + ref<Dotted>> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<PATH>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
