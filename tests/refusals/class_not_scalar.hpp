// A surrogate is no scalar: no class holds it, and the spelling
// does not compile.
// expect LR: names a value that is no scalar
#pragma once

#include "refusals/head.hpp"

namespace refuse {
struct G : Base {
  struct NAME : terminal<some(cr<R"(a-z\u{D800})">)> {};
  struct Root : rule<create<ast::Node>(
      assign<&ast::Node::name>(ref<NAME>))> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace refuse
