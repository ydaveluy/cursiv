// End of input asserted twice in one choice: the second says nothing.
// expect LR LL: refused .twoEndMarks..*rule .Root. asserts end of input twice
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>) +
                                       (eof | eof))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
