// A guard whose excluded word `{-##` reaches two bytes into the text between a
// `mark` and its `same`: only the first of those bytes can be checked.
// expect LR: refused: terminal .CMT. has a guard whose excluded word reaches into the text between a .mark<>. and its .same<>.
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
