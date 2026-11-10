// A commit whose prefix reads a left-recursive terminal: the error names the
// terminal, not the prefix.
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
