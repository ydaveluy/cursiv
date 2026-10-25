// An indented grammar embedding another, its indentation spelled in the body
// of a rule rather than as terminals it names.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct Inner : cursiv::GrammarSpec {
  struct SP : terminal<some(cr<" ">)> {};
  struct NUM : terminal<some(d)> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<NUM>))> {};
  using entry = Root;
  using ignore = SP;
};

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t)">)> {};
  struct NEWLINE : terminal<eol> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Root
      : rule<ref<ID> + ref<NEWLINE> + indent +
             section<Inner>(lit<"<">, lit<">">) + ref<NEWLINE> + dedent> {};
  using entry = Root;
  using ignore = WS;
  using indent_newline = NEWLINE;
};

}  // namespace refuse
