// A run written inside a run: the generation goes on, and names the
// repetition.
// expect LR: warning: rule .Root.: .*repeats a repetition, so a run of items can be split into groups in several ways
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct SEMI : terminal<lit<";">> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>) +
                                       many(some(ref<SEMI>)))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
