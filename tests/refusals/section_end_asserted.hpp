// A section closed by a `]` that another `]` follows, whose grammar reads
// `]]`: where the assertion holds, the closing token begins a token of the
// grammar, but the check does not read whether it does, so the grammar is
// generated with a warning.
// expect LR LL: warning: terminal ... closes a .section<>., but the generator could not check that ...., a terminal of the embedded grammar, neither matches it nor starts with it
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct Inner : cursiv::GrammarSpec {
  struct SP : terminal<some(cr<" ">)> {};
  struct NUM : terminal<some(d)> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<NUM>) +
                                       option(lit<"]]"> + ref<NUM>))> {};
  using entry = Root;
  using ignored = SP;
};

struct G : Base {
  struct Root
      : rule<section<Inner>(lit<"[">, lit<"]", &lit<"]">>) + lit<"]">> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
