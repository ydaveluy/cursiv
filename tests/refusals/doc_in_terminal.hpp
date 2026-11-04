// A `doc` in a terminal: it goes on a literal a rule reads.
// expect LR: a .doc. goes on a literal a rule reads, not inside a terminal or a guard
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct IFF : terminal<kw<"iff">.doc<"a conditional">()> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<IFF>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
