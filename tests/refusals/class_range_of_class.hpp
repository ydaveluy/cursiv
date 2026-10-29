// A range runs between two characters: a class escape at one of its ends
// names no range, and the spelling does not compile.
// expect LR: uses a class escape .* as an end of a range
#pragma once

#include "refusals/head.hpp"

namespace refuse {
struct G : Base {
  struct NAME : terminal<some(cr<R"(a-\d)">)> {};
  struct Root : rule<create<ast::Node>(
      assign<&ast::Node::name>(ref<NAME>))> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace refuse
