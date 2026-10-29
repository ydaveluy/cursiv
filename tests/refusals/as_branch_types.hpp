// In a terminal, the alternatives' constants give one token its value, so
// they have one type.
// expect LR: inside a terminal, the constants of the alternatives. as<V>.e. have different types
#pragma once

#include "refusals/head.hpp"

namespace refuse {
struct G : Base {
  struct W : terminal<as<1>(kw<"one">) | as<2u>(kw<"two">)> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<W>))> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace refuse
