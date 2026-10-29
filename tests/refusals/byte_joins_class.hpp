// `byte` is one raw byte, not a class: no class joins it.
// expect LR: byte. is one raw byte, not a class, so it cannot be joined to a class
#pragma once

#include "refusals/head.hpp"

namespace refuse {
struct G : Base {
  struct NAME : terminal<cr<"a-z"> | byte> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<NAME>))> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace refuse
