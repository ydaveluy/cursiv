// expect LR: refused: terminal .MARKED. uses .same<>. where a .mark<>. with its tag is not set on every path before it
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Level {};
  struct Group : terminal<lit<"("> + option(ref<Group>) + lit<")">> {};
  struct MARKED : terminal<ref<Group> + same<Level>> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<MARKED>))> {
  };
  using entry = Root;
};

}  // namespace refuse
