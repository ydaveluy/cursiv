// A directive over a choice distributes: an alternative that produces no
// value is refused beside one that does, not crashed on at the first document.
// expect LR: stands over a shape that produces no value
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
