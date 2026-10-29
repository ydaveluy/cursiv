// expect LR: terminal .EMPTY. matches the empty string
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct EMPTY : terminal<option(lit<"a"> + ref<EMPTY>)> {};
  struct Root : rule<create<ast::Node>(some(ref<EMPTY> + lit<";">))> {};
  using entry = Root;
};

}  // namespace refuse
