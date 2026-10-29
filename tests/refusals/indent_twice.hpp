// Two indent terminals: the indentation answers one of each.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t)">)> {};
  struct NEWLINE : terminal<eol> {};
  struct INDENT : terminal<indent> {};
  struct OPEN : terminal<indent> {};
  struct DEDENT : terminal<dedent> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Root
      : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>) +
                               ref<NEWLINE> + (ref<INDENT> | ref<OPEN>) +
                               ref<ID> + ref<NEWLINE> + ref<DEDENT>)> {};
  using entry = Root;
  using ignored = WS;
  using indent_newline = NEWLINE;
};

}  // namespace refuse
