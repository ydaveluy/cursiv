// `as<T>` makes a `T`, and an `int8` cannot hold every `int64`.
// expect LR: this rule gives a number this member cannot always hold
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace cut {
struct Node : cursiv::Extends<cursiv::AstNode> {
  std::int8_t value = 0;
};
}  // namespace cut
struct G : Base {
  struct NUM : terminal<as<std::int64_t>(some(d))> {};
  struct Root : rule<create<cut::Node>(assign<&cut::Node::value>(ref<NUM>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
