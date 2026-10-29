// Two trivia that share `[[`, one ignored and read on by a capture, one
// hidden: they never read the same text, but the check does not follow the
// capture, so the grammar is generated with a warning.
// expect LR LL: warning: terminals .LONG. and .TAG. are trivia, and the check that they never read the same text at the same length and rank could not be completed
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct Eq {};

struct G : Base {
  struct LONG : terminal<through(lit<"["> + mark<Eq>(many(lit<"=">)) + lit<"[">,
                                 lit<"]"> + same<Eq> + lit<"]">)> {};
  struct TAG : terminal<lit<"[["> + some(d)> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>))> {};
  using entry = Root;
  using ignored = list<WS, LONG>;
  using hidden = TAG;
};

}  // namespace refuse
