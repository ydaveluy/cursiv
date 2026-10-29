// A recursive call cannot return to its start without consuming text.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct PATH;
  struct DOTTED : terminal<ref<PATH> + lit<"."> + ref<ID>> {};
  struct PATH : terminal<ref<DOTTED> | ref<ID>> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<PATH>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
