// Two trivia that read one space, one ignored and unguarded and one hidden and
// guarded: a guard adds no rank.
// expect LR: refused: terminal .NOTE. is trivia that reads the same text as another trivia terminal
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct BLANK : terminal<some(s)> {};
  struct NOTE : terminal<some(s) + !lit<"y">> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>))> {};
  using entry = Root;
  using ignored = BLANK;
  using hidden = NOTE;
};

}  // namespace refuse
