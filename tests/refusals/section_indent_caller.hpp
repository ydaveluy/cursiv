// An indented grammar embedding another: the section's lines would be
// measured as the document's.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct Inner : cursiv::GrammarSpec {
  struct SP : terminal<some(cr<" ">)> {};
  struct NUM : terminal<some(d)> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<NUM>))> {};
  using entry = Root;
  using ignored = SP;
};

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t)">)> {};
  struct NEWLINE : terminal<eol> {};
  struct INDENT : terminal<indent> {};
  struct DEDENT : terminal<dedent> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Root
      : rule<ref<ID> + ref<NEWLINE> + ref<INDENT> +
             section<Inner>(lit<"<">, lit<">">) + ref<NEWLINE> + ref<DEDENT>> {
  };
  using entry = Root;
  using ignored = WS;
  using indent_newline = NEWLINE;
};

}  // namespace refuse
