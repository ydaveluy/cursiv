// `+=` accumulates, so it names a member the grammar can accumulate into: on a
// single-valued member each match would overwrite the last.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Root
      : rule<create<ast::Node>(many(append<&ast::Node::name>(ref<ID>)))> {};
  using entry = Root;
  using ignore = WS;
};

}  // namespace refuse
