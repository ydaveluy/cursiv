// A grammar read by its indentation, embedded: its lines would be measured
// against the embedding document's.
// expect LR: a grammar embedded with .section<>. cannot use indentation
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct Inner : cursiv::GrammarSpec {
  struct SP : terminal<some(cr<" ">)> {};
  struct NUM : terminal<some(d)> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<NUM>) +
                                       option(indent))> {};
  using entry = Root;
  using ignored = SP;
};

struct G : Base {
  struct Root : rule<section<Inner>(lit<"<">, lit<">">)> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
