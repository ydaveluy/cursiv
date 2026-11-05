// A built string a data rule passes on is no number: the member cannot read it
// as one.
// expect LR: no conversion from this value to the member type
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
  int64 target = 0;
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
