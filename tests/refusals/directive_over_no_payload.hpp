// A directive over a quantifier that holds no node: it gives no value, so the
// member would be written from nothing.
// expect LR at compile: applies to an expression that gives no value
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Root
      : rule<create<ast::Node>(assign<&ast::Node::name>(option(ref<ID>)))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
