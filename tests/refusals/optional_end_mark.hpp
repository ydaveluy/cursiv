// An end assertion that may be absent: the reading that keeps it refuses
// everything past it, the reading that drops it is the same production with no
// condition at all.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>) +
                                       option(eof))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
