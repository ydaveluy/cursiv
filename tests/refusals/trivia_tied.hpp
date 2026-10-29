// Two trivia that read one space, one ignored and one hidden: the tree would
// keep one of them by the order they are declared in.
// expect LR: refused: terminal .(WS|NOTE). is trivia that matches the same text as another trivia terminal
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct NOTE : terminal<some(s)> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>))> {};
  using entry = Root;
  using ignored = WS;
  using hidden = NOTE;
};

}  // namespace refuse
