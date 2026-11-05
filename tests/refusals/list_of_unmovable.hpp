// A list moves what it holds as it grows, and this type cannot be moved.
// expect LR: a list moves its elements as it grows
#pragma once

#include <string>
#include <string_view>

#include "refusals/head.hpp"

namespace refuse {

namespace fixed {
struct Pin {
  std::string text;
  explicit Pin(std::string value) : text(std::move(value)) {}
  Pin(const Pin&) = delete;
  Pin& operator=(const Pin&) = delete;
};
struct Node : cursiv::Extends<cursiv::AstNode> {
  vector<Pin> pins;
};
inline std::string word(std::string_view text) { return std::string(text); }
}  // namespace fixed
struct G : Base {
  struct NAME : terminal<as<&fixed::word>(some(cr<"a-z">))> {};
  struct Root
      : rule<create<fixed::Node>(many(append<&fixed::Node::pins>(ref<NAME>)))> {
  };
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
