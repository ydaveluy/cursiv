// A grammar whose indentation is a terminal it names, embedded: its lines would
// be measured against the embedding document's.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct Inner : cursiv::GrammarSpec {
  struct SP : terminal<some(cr<" ">)> {};
  struct NUM : terminal<some(d)> {};
  struct INDENT : terminal<indent> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<NUM>) +
                                       option(ref<INDENT>))> {};
  using entry = Root;
  using ignored = SP;
};

struct G : Base {
  struct Root : rule<section<Inner>(lit<"<">, lit<">">)> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
