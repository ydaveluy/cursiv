// A terminal reading a rule that builds a node: a rule is read with the
// skipper between its parts, and a terminal is the bytes of one token.
// expect LR: refused: the grammar uses the rule .(Pair|Dotted). inside a terminal or a .through.: these read one token
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Pair : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>))> {};
  struct PAIR : terminal<ref<ID> + lit<":"> + ref<Pair>> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<PAIR>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
