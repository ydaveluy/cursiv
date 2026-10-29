// expect LR: `section<G>.open, close.` is allowed in a rule, not in a terminal
#pragma once

#include "refusals/head.hpp"

namespace refuse {
struct Guest : cursiv::GrammarSpec {
  struct WORD : terminal<some(cr<"a-z">)> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<WORD>))> {};
  using entry = Root;
};
struct G : Base {
  struct T : terminal<lit<"a"> + section<Guest>(lit<"[">, lit<"]">)> {};
  struct Root : rule<create<ast::Node>(ref<T>)> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace refuse
