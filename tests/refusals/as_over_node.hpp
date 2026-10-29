// `as` gives a value to text: around a rule that builds a node, the node
// would be built and lost.
#pragma once

#include "refusals/head.hpp"

namespace refuse {
enum class Mode { on, off };
struct G : Base {
  struct Item : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>))> {};
  struct Flag : rule<as<Mode::on>(kw<"on"> + ref<Item>)> {};
  struct Root : rule<create<ast::Node>(ref<Flag>)> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace refuse
