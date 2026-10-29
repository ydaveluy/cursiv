// A commit beside other items: it stands only as the item of a quantifier.
// expect LR: commit.prefix, body. must be the direct item of option, many, some or repeat
#pragma once

#include "refusals/head.hpp"

namespace refuse {
struct G : Base {
  struct Root : rule<create<ast::Node>(commit(lit<"a">, lit<"b">) + lit<"c">)> {
  };
  using entry = Root;
  using ignored = WS;
};
}  // namespace refuse
