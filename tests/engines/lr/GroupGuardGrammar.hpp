#pragma once

// The empty rule a tight mode reduces on a blank, where the head before it
// refuses its goto: split in groups of states, the parse answers as a small
// one's. And a state whose only action reduces such a rule.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "engines/lr/GuardedChoiceGrammar.hpp"

namespace tgroupguard {

namespace ast {
struct Doc : cursiv::Extends<cursiv::AstNode> {};
}  // namespace ast

namespace small {
struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Bang : rule<option(lit<"x">) + !lit<"z">> {};
  struct Item : rule<lit<"p"> +
                     tight(lit<"a"> + &lit<"x"> + ref<Bang>) +
                     lit<"c">> {};
  struct Root : rule<create<ast::Doc>(ref<Item>)> {};
  using entry = Root;
  using ignore = WS;
};
}  // namespace small

namespace landing {
struct E : cursiv::Extends<cursiv::AstNode> {};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  E* e = nullptr;
};

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct B : rule<create<E>> {};
  struct C : rule<ref<B> + lit<"y">> {};
  struct Root : rule<create<Doc>(lit<"p"> + &lit<"y"> +
                                 assign<&Doc::e>(ref<C>) + lit<"x">)> {};
  using entry = Root;
  using ignore = WS;
};
}  // namespace landing

// Both empty reductions share one generated table, split into small groups.
struct G : small::G {
  struct Case : rule<ref<small::G::Root> |
                     (kw<"landing"> + ref<landing::G::Root>) |
                     ref<tguardedchoice::Named::Root>> {};
  using entry = Case;
};

}  // namespace tgroupguard
