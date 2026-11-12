// A `string_view` member over a converter that builds a string: in an optional.
// expect LR at compile: a string_view member cannot hold this value
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
  optional<string_view> name;
};
}  // namespace view
struct G : Base {
  struct LOUD : terminal<as<view::shout>(some(cr<"A-Z">))> {};
  struct Root : rule<create<view::Node>(assign<&view::Node::name>(ref<LOUD>))> {
  };
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
