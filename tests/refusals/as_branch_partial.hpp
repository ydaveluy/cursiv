// In a terminal, either every alternative has its own `as` or none has.
// expect LR: inside a terminal, as<X>.e. wraps the whole body or each of its alternatives
#pragma once

#include "refusals/head.hpp"

namespace refuse {
struct G : Base {
  struct W : terminal<as<true>(kw<"yes">) | kw<"no">> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<W>))> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace refuse
