// A word refused one byte into a nested run that the end of the text closes:
// the run starts behind that byte, and a text ending in front of it has no run.
// expect LR: refused: terminal .CMT. has a guard whose excluded word reaches into the text between a .mark<>. and its .same<>.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct OPEN : terminal<lit<"(*"> +
                         many(ref<OPEN> | (!lit<"(*"> + !lit<"*)"> + any)) +
                         (lit<"*)"> | eof)> {};
  struct CMT : terminal<some(cr<"x">) + !lit<"(*!"> + ref<OPEN>> {};
  struct Root
      : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>) + ref<CMT>)> {
  };
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
