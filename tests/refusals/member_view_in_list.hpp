// A `string_view` member over a converter that builds a string: in a list.
// expect LR: a string_view member cannot hold this value
#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "cursiv/grammar/Conversion.hpp"
#include "refusals/head.hpp"

namespace refuse {

namespace view {
inline constexpr auto shout = [](std::string_view text) {
  return std::string(text);
};
struct Node : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> names;
};
}  // namespace view
struct G : Base {
  struct LOUD : terminal<as<view::shout>(some(cr<"A-Z">))> {};
  struct Root
      : rule<create<view::Node>(many(append<&view::Node::names>(ref<LOUD>)))> {
  };
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
