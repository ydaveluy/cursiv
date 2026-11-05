// `as<T>` gives a value: a number is not text a `string` can hold.
// expect LR: no conversion from this value to the member type
#pragma once

#include <string>

#include "refusals/head.hpp"

namespace refuse {

namespace typed {
struct Node : cursiv::Extends<cursiv::AstNode> {
  string value;
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
