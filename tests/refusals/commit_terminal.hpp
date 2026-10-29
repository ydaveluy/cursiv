// A commit in a terminal: a terminal reads adjacent bytes, with no condition
// to stop a repetition on.
// expect LR: a commit is allowed in a rule, not in a terminal
#pragma once

#include "refusals/head.hpp"

namespace refuse {
struct G : Base {
  struct W : terminal<many(commit(lit<"a">, lit<"b">))> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<W>))> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace refuse
