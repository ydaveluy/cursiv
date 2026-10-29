// A commit whose prefix may read nothing: it is always there to be read, so
// the quantifier could never stop.
// expect LR: rule .Root. has a .commit. whose prefix may match nothing
#pragma once

#include "refusals/head.hpp"

namespace refuse {
struct G : Base {
  struct Root : rule<create<ast::Node>(
                    many(commit(option(lit<"a">), lit<"b">)) + lit<"c">)> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace refuse
