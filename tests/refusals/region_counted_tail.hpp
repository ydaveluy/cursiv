// A region ending on a counted repetition, followed by `eof`: past the minimum
// count, the parser cannot tell another turn inside the region from the end
// of the text, read outside it with another skipper.
// expect LR LL: refused: the grammar reaches the same token both inside a .* region and outside it
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Tail
      : rule<tight(lit<"a"> + repeat<2, 4>(lit<"b">))> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>) +
                                       ref<Tail> + eof)> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
