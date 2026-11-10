// A condition that tests a data rule calling itself: a condition expands what
// it tests, and this expansion never ends.
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
