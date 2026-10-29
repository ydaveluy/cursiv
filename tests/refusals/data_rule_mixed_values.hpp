// A data rule passes the value of the alternative it took, so its
// alternatives pass one type: here a number and a text.
// expect LR: the alternatives of this data rule give values of different types
#pragma once

#include <cstdint>

#include "refusals/head.hpp"

namespace refuse {
struct G : Base {
  struct NUM : terminal<as<std::int64_t>(some(d))> {};
  struct Value : rule<ref<NUM> | ref<ID>> {};
  struct Root
      : rule<create<ast::Node>(assign<&ast::Node::name>(ref<Value>))> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace refuse
