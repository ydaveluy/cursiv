// Two implied forms of one token: a warning names the branches as written, the
// token and its implied reading being one branch.
// expect LR: warning: rule .Root.: branch 2 of 2 .';'. matches the same tokens as branch 1, so it is never chosen and is dropped
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Root
      : rule<create<ast::Node>(
            assign<&ast::Node::name>(ref<ID>) +
            (implied(lit<";">, gap::line) | implied(lit<";">, eof)))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
