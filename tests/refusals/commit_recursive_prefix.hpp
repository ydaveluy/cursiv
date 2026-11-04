// A commit whose prefix reads a terminal that starts with itself: the terminal
// is refused, and no question about the prefix stands in the way.
// expect LR LL: refused.*terminal .T. calls itself without consuming input
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct T : terminal<(ref<T> + lit<"a">) | lit<"b">> {};
  struct Root : rule<create<ast::Node>(
                    many(commit(ref<T> + lit<";">, lit<":"> + ref<ID>)) +
                    assign<&ast::Node::name>(ref<ID>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
