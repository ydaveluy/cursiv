// A condition that reads a data rule reaching itself: what it reads is
// unrolled once, and the rule never ends.
// expect LR: a condition cannot test a data rule that calls itself
#pragma once

#include "refusals/head.hpp"

namespace refuse {
struct G : Base {
  struct Call : rule<ref<ID> + option(lit<"("> + ref<Call> + lit<")">)> {};
  struct Root : rule<create<ast::Node>(!ref<Call> +
                                       assign<&ast::Node::name>(ref<ID>))> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace refuse
