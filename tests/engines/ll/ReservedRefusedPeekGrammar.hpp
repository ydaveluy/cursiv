#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tllrefusedpeek {
struct Item : cursiv::Extends<cursiv::AstNode> {
  bool keyword = false;
  string_view name;
  string_view tag;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Item*> items;
};

// The refused keyword of `tllrefusedglr`, the tag met first and so the token in
// hand where the name is asked of by a lookahead: a keyword refused at the
// position reserves nothing from the question either.
struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct IN : terminal<!kw<"in-place"> + kw<"in">> {};
  struct TAG : terminal<cr<"a-z"> + many(w)> {};
  struct NAME : terminal<cr<"a-z"> + many(w)> {};
  struct Part
      : rule<create<Item>(flag<&Item::keyword>(ref<IN>) |
                          (assign<&Item::tag>(ref<TAG>) + lit<"!">) |
                          (&(ref<NAME> | ref<IN> | (ref<TAG> + lit<"?">)) +
                           assign<&Item::tag>(ref<TAG>) + lit<"-">) |
                          (assign<&Item::name>(ref<NAME>) + lit<".">))> {};
  struct Root : rule<create<Doc>(many(append<&Doc::items>(ref<Part>)))> {};
  using entry = Root;
  using ignored = WS;
};
struct Ll : Base {};
struct Lr : Base {};
}  // namespace tllrefusedpeek
