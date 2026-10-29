// In a terminal, the alternatives' constants are the token's value: an `as`
// around them would be a second one.
// expect LR: inside a terminal, as<X>.e. must wrap the whole body or each of its alternatives
#pragma once

#include "refusals/head.hpp"

namespace refuse {
struct G : Base {
  struct W : terminal<as<bool>(as<true>(kw<"yes">) | as<false>(kw<"no">))> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<W>))> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace refuse
