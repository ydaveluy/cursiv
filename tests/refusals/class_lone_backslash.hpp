// A backslash opens an escape: one that ends the spelling escapes nothing,
// and the spelling does not compile.
// expect LR: ends on a lone backslash
#pragma once

#include "refusals/head.hpp"

namespace refuse {
struct G : Base {
  struct NAME : terminal<some(cr<R"(a-z\)">)> {};
  struct Root : rule<create<ast::Node>(
      assign<&ast::Node::name>(ref<NAME>))> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace refuse
