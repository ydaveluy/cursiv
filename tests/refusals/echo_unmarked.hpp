// A lookahead cannot echo a tag absent from the enclosing token.
// expect LR: refused: terminal .TAG. uses .same<>. where a .mark<>. with its tag is not set on every path before it
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct Fence {};
struct Other {};

struct G : Base {
  struct TAG : terminal<lit<"<"> + mark<Fence>(some(cr<"a-z">)) + lit<"-"> +
                        &same<Other> + some(cr<"a-z">) + lit<">">> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<TAG>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
