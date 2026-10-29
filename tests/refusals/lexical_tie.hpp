// `**` reads as both terminals where one state shifts either.
// expect LR: candidates mode=[0-9]+ .(STARS|BOLD). -> .(STARS|BOLD). on .[*][*].
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct STARS : terminal<some(lit<"*">)> {};
  struct BOLD : terminal<lit<"**">> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>)) +
                     many(ref<BOLD> | ref<STARS>)> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
