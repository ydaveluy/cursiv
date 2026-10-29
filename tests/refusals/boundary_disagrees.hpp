// One match reached through two refusals of different classes: after `ab` the
// byte may be neither `x` nor `y`, and the table tests one class after a match.
// expect LR: refused: terminal .T. reaches the same match through two guards that exclude different characters after it
// expect LL: refusal_boundary_disagrees_ll: terms=
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct T : terminal<lit<"ab", !cr<"x">> | lit<"ab", !cr<"y">>> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<T>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
