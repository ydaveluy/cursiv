// A nested comment hidden and a delimited one ignored in a skip region: both
// read `/* x */`, so the tree would keep the comment by the order they are
// declared in.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct BLOCK : terminal<through(lit<"/*">, lit<"*/">)> {};
  struct NOTE
      : terminal<lit<"/*"> +
                 many(ref<NOTE> | (!lit<"/*"> + !lit<"*/"> + any)) +
                 lit<"*/">> {};
  struct Root : rule<create<ast::Node>(
                    skip(ignore<WS, BLOCK>, hide<NOTE>,
                         lit<"("> + assign<&ast::Node::name>(ref<ID>) +
                             lit<")">))> {};
  using entry = Root;
  using ignored = WS;
  using hidden = NOTE;
};

}  // namespace refuse
