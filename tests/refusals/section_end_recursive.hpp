// A section closed by `END`, whose grammar reads `END` with a terminal that
// calls itself: the closing token is a token of the grammar, but the check
// does not follow a call, so the grammar is generated with a warning.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct Inner : cursiv::GrammarSpec {
  struct REGION : terminal<lit<"E"> + option(ref<REGION>) + lit<"ND">> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<REGION>))> {
  };
  using entry = Root;
};

struct G : Base {
  struct Root : rule<section<Inner>(lit<"<">, lit<"END">)> {};
  using entry = Root;
};

}  // namespace refuse
