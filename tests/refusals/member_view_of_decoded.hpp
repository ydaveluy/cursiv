// A `string_view` member over a data rule that decodes a terminal's slice.
// expect LR: a string_view member cannot hold this value
#pragma once

#include <string>

#include "refusals/head.hpp"

namespace refuse {

namespace view {
inline constexpr auto slice = [](std::string_view text) {
  return text.substr(1);
};
inline constexpr auto decode = [](std::string_view text) {
  return std::string(text);
};
struct Node : cursiv::Extends<cursiv::AstNode> {
  string_view value;
};
}  // namespace view
struct G : Base {
  struct QUOTED : terminal<as<view::slice>(lit<"'"> + some(cr<"a-z">))> {};
  struct Decoded : rule<as<view::decode>(ref<QUOTED>)> {};
  struct Root
      : rule<create<view::Node>(assign<&view::Node::value>(ref<Decoded>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
