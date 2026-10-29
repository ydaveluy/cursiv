// In a terminal, either every alternative has its own `as` or none has.
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
