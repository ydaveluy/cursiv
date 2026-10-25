#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct LOOP : terminal<lit<"a"> + ref<LOOP>> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<LOOP>))> {};
  using entry = Root;
};

}  // namespace refuse
