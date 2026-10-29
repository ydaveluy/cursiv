// Two optional items reading the same tokens and writing different members, in
// a sequence so wide that the gate spells its first positions as auxiliaries:
// the second read in place of the first is named and dropped as in a narrow
// one.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace gated {
struct Node : cursiv::Extends<cursiv::AstNode> {
  cursiv::AstNode::string_view a, b;
};
}  // namespace gated
struct G : Base {
  struct Root
      : rule<create<gated::Node>(
            kw<"r"> + option(assign<&gated::Node::a>(ref<ID>)) +
            option(assign<&gated::Node::b>(ref<ID>)) + option(lit<"1">) +
            option(lit<"2">) + option(lit<"3">) + option(lit<"4">) +
            option(lit<"5">) + option(lit<"6">) + option(lit<"7">))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
