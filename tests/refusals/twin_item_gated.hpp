// Two optional items reading the same tokens, at the start of a long sequence
// of options: the second is still reported and dropped.
// expect LR LL: warning: rule .Root.: ..b=ID... matches the same tokens as the earlier ..a=ID..., so it is never chosen in place of that item
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace gated {
struct Node : cursiv::Extends<cursiv::AstNode> {
  string_view a, b;
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
