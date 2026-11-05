// A `string_view` member over a converter that builds a string: as a variant's
// only text.
// expect LR: a variant member takes a value that matches exactly one
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
  variant<string_view, int64> value;
};
}  // namespace view
struct G : Base {
  struct LOUD : terminal<as<view::shout>(some(cr<"A-Z">))> {};
  struct Root
      : rule<create<view::Node>(assign<&view::Node::value>(ref<LOUD>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
