// In a terminal, an alternative's own `as` names a constant: a type or a
// function converts the whole body.
// expect LR: inside a terminal, as<V>.e. on an alternative names a constant
#pragma once

#include "refusals/head.hpp"

namespace refuse {
struct G : Base {
  struct W : terminal<as<bool>(kw<"yes">) | as<bool>(kw<"no">)> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<W>))> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace refuse
