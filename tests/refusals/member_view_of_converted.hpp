// A `string_view` member over a converter that builds a string.
// expect LR at compile: a string_view member cannot hold this value
#pragma once

#include <string>

#include "refusals/head.hpp"

namespace refuse {

namespace view {
inline constexpr auto shout = [](std::string_view text) {
  return std::string(text);
};
}  // namespace view
struct G : Base {
  struct LOUD : terminal<as<view::shout>(some(cr<"A-Z">))> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<LOUD>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
