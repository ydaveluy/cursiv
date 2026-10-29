// In a terminal, the alternatives' constants give one token its value, so
// they have one type.
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
