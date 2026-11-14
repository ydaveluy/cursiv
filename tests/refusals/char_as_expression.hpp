// A character where an expression is expected: a literal is written `lit<"x">`.
// expect LR at compile: a grammar expression is joined to something that is not one
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Root
      : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>)) + 'x'> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
