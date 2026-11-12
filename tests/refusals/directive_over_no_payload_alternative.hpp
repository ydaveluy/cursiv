// A directive over a choice applies to each alternative: one that gives no
// value is refused, even beside one that does.
// expect LR at compile: applies to an expression that gives no value
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Root
      : rule<create<ast::Node>(
            lit<"operator"> +
            assign<&ast::Node::name>(ref<ID> | many(lit<"[">) | lit<"+">))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
