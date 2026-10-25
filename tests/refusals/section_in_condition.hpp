#pragma once

#include "refusals/head.hpp"

namespace refuse {
struct Guest : cursiv::GrammarSpec {
  struct WORD : terminal<some(cr<"a-z">)> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<WORD>))> {};
  using entry = Root;
};
struct G : Base {
  struct Root
      : rule<create<ast::Node>(&section<Guest>(lit<"[">, lit<"]">) +
                               assign<&ast::Node::name>(ref<ID>))> {};
  using entry = Root;
  using ignore = WS;
};
}  // namespace refuse
