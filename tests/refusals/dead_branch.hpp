// A branch that reads a token past `eof`, beside one that stands: the
// generation goes on, and names the branch no text reaches.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct SEMI : terminal<lit<";">> {};
  struct BAR : terminal<lit<"|">> {};
  struct Para : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>) +
                                       many((ref<SEMI> + ref<ID>) |
                                            (ref<BAR> + eof + ref<SEMI>)))> {};
  using entry = Para;
  using ignored = WS;
};

}  // namespace refuse
