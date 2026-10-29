// In a terminal, `as` converts the whole token, never a part of it.
// expect LR: inside a terminal, as<X>.e. wraps the whole body or each of its alternatives
#pragma once

#include "refusals/head.hpp"

namespace refuse {
enum class Mode { on, off };
struct G : Base {
  struct W : terminal<lit<"x"> + as<Mode::on>(lit<"y">)> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<W>))> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace refuse
