// `+=` over a rule that returns the whole list: it adds one element.
// expect LR at compile: adds one element, and this
#pragma once

#include <string>
#include <vector>

#include "refusals/head.hpp"

namespace refuse {

namespace whole {
inline constexpr auto split = [](std::string_view text) {
  return std::vector<std::string>{std::string(text)};
};
struct Node : cursiv::Extends<cursiv::AstNode> {
  vector<string> names;
};
}  // namespace whole
struct G : Base {
  struct LIST : terminal<as<whole::split>(some(cr<"A-Z">))> {};
  struct Root : rule<create<whole::Node>(
                    many(append<&whole::Node::names>(ref<LIST>)))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
