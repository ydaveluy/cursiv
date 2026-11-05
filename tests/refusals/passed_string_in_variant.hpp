// A built string a data rule passes on is no number: a variant cannot read it
// as one.
// expect LR: a variant member takes a value that matches exactly one
#pragma once

#include <string>
#include <variant>

#include "refusals/head.hpp"

namespace refuse {

namespace pass {
inline constexpr auto built = [](std::string_view text) {
  return std::string(text);
};
struct Node : cursiv::Extends<cursiv::AstNode> {
  variant<int64, string_view> target;
};
}  // namespace pass
struct G : Base {
  struct NAME : terminal<as<pass::built>(some(cr<"a-z">))> {};
  struct Name : rule<ref<NAME>> {};
  struct Root
      : rule<create<pass::Node>(assign<&pass::Node::target>(ref<Name>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
