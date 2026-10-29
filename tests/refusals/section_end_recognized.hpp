// A section closed by `a!`, whose grammar reads runs of `a` around a `?` with
// a terminal that calls itself: no text of it starts with `a!`, but the check
// does not follow a call, so the grammar is generated with a warning.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct Inner : cursiv::GrammarSpec {
  struct SP : terminal<some(cr<" ">)> {};
  struct RUN : terminal<lit<"a"> + (ref<RUN> | lit<"?">) + lit<"a">> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<RUN>))> {};
  using entry = Root;
  using ignored = SP;
};

struct G : Base {
  struct Root : rule<section<Inner>(lit<"<">, lit<"a!">)> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
