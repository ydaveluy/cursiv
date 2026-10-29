// A region that skips the newlines its caller reads as tokens, and may stop
// after its first name: the point after that name reads a newline two ways.
// expect LR LL: refused: the grammar reaches the same token both inside a .* region and outside it
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct NL : terminal<some(lit<"\n">)> {};
  struct Root
      : rule<create<ast::Node>(skip(ignore<WS, NL>, hide<>,
                                    lit<"("> + ref<ID> + option(ref<ID>)) +
                               ref<NL>)> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
