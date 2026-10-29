// Eleven items in any order, the most either engine spells: a rule and three
// positions for every subset in the descent, a production for every subset in
// the table.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Root : rule<create<ast::Node>(
                    unordered(lit<"a01">, lit<"a02">, lit<"a03">, lit<"a04">,
                              lit<"a05">, lit<"a06">, lit<"a07">, lit<"a08">,
                              lit<"a09">, lit<"a10">, lit<"a11">))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
