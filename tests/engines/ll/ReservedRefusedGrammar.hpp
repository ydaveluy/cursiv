#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tllrefused {
struct Item : cursiv::Extends<cursiv::AstNode> {
  bool keyword = false;
  bool dash = false;
  string_view name;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Item*> items;
};

// A keyword refusing a longer word spelled at its head, and a name it
// reserves: where the keyword is refused it stands nowhere and reserves
// nothing.
struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct IN : terminal<!kw<"in-place"> + kw<"in">> {};
  struct NAME : terminal<cr<"a-z"> + many(w)> {};
  struct Part : rule<create<Item>(flag<&Item::keyword>(ref<IN>) |
                                  assign<&Item::name>(ref<NAME>) |
                                  flag<&Item::dash>(lit<"-">))> {};
  struct Root : rule<create<Doc>(many(append<&Doc::items>(ref<Part>)))> {};
  using entry = Root;
  using ignore = WS;
};
struct Ll : Base {};
struct Lr : Base {};
}  // namespace tllrefused
