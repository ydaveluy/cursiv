// Nothing can replace the first value of a member that cannot be assigned.
// expect LR: this member's type cannot be assigned
#pragma once

#include <string>
#include <string_view>

#include "refusals/head.hpp"

namespace refuse {

namespace frozen {
struct Seal {
  const std::string text;
  explicit Seal(std::string value) : text(std::move(value)) {}
};
inline Seal seal(std::string_view text) { return Seal(std::string(text)); }
struct Node : cursiv::Extends<cursiv::AstNode> {
  Seal value{"none"};
};
}  // namespace frozen
struct G : Base {
  struct NAME : terminal<as<&frozen::seal>(some(cr<"a-z">))> {};
  struct Root
      : rule<create<frozen::Node>(assign<&frozen::Node::value>(ref<NAME>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
