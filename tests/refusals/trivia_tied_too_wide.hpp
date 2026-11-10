// Two trivia whose comparison grows past its budget: the check stops unsure,
// and the lexer table, too large to build, is refused without leaking memory.
// expect LR: refused: the grammar has terminals too complex for the generated lexer
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct C1 : terminal<lit<"#"> + many(cr<"ab">) + lit<"a"> +
                       repeat<24>(cr<"ab">)> {};
  struct C2 : terminal<lit<"#"> + many(cr<"ab">) + lit<"b"> +
                       repeat<25>(cr<"ab">)> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>))> {};
  using entry = Root;
  using ignored = WS;
  using hidden = list<C1, C2>;
};

}  // namespace refuse
