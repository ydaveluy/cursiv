// A word refused two bytes into a relay's content: the walk reads only the
// first byte of it, and no edge of the token graph narrows the rest.
// expect LR: refused: terminal .CMT. refuses a word that runs into a relay's content
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Level {};
  struct CMT
      : terminal<some(lit<"!">) + !lit<"{-##"> +
                 (through(lit<"{"> + mark<Level>(many(cr<"#">)) + lit<"-">,
                          lit<"-"> + same<Level> + lit<"}">))> {};
  struct Root
      : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>) + ref<CMT>)> {
  };
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
