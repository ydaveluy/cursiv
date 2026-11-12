// Nothing can replace the first value of a member that cannot be assigned.
// expect LR at compile: this member's type cannot be assigned
#pragma once

#include <string>
#include <string_view>

#include "refusals/head.hpp"

namespace refuse {

namespace frozen {
struct ConstText {
  const std::string text;
  explicit ConstText(std::string value) : text(std::move(value)) {}
};
inline ConstText const_text(std::string_view text) {
  return ConstText(std::string(text));
}
struct Node : cursiv::Extends<cursiv::AstNode> {
  ConstText value{"none"};
};
}  // namespace frozen
struct G : Base {
  struct NAME : terminal<as<&frozen::const_text>(some(cr<"a-z">))> {};
  struct Root
      : rule<create<frozen::Node>(assign<&frozen::Node::value>(ref<NAME>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
