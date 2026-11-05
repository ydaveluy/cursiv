// An enumerator is a number, and this one does not fit the member.
// expect LR: this rule gives a number this member cannot always hold
#pragma once

#include <string_view>

#include "refusals/head.hpp"

namespace refuse {

namespace bigenum {
enum Big { huge = 1000 };
inline Big big(std::string_view) { return huge; }
struct Node : cursiv::Extends<cursiv::AstNode> {
  std::int8_t value = 0;
};
}  // namespace bigenum
struct G : Base {
  struct NAME : terminal<as<&bigenum::big>(some(cr<"a-z">))> {};
  struct Root
      : rule<create<bigenum::Node>(assign<&bigenum::Node::value>(ref<NAME>))> {
  };
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
