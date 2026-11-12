// `as<T>` gives a value: a number is not text a `string_view` can hold.
// expect LR at compile: a string_view member cannot hold this value
#pragma once

#include <string>

#include "refusals/head.hpp"

namespace refuse {

namespace typed {
struct Node : cursiv::Extends<cursiv::AstNode> {
  string_view value;
};
}  // namespace typed
struct G : Base {
  struct NUM : terminal<as<std::int64_t>(some(d))> {};
  struct Root
      : rule<create<typed::Node>(assign<&typed::Node::value>(ref<NUM>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
