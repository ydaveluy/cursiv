// expect LR: refused:.*calls itself without consuming input
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct LOOP : terminal<(option(lit<"a">) + ref<LOOP>) | lit<"b">> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<LOOP>))> {};
  using entry = Root;
};

}  // namespace refuse
