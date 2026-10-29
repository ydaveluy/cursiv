#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tllrefusedglr {
struct Item : cursiv::Extends<cursiv::AstNode> {
  bool keyword = false;
  string_view name;
  string_view tag;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Item*> items;
};

// The refused keyword of `tllrefused` in a mode that reads several
// candidates: two names spell the same words, and the byte behind a word
// says which one it is.
struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct IN : terminal<!kw<"in-place"> + kw<"in">> {};
  struct NAME : terminal<cr<"a-z"> + many(w)> {};
  struct TAG : terminal<cr<"a-z"> + many(w)> {};
  struct Part
      : rule<create<Item>(flag<&Item::keyword>(ref<IN>) |
                          (assign<&Item::name>(ref<NAME>) + lit<".">) |
                          (assign<&Item::tag>(ref<TAG>) + lit<"-">))> {};
  struct Root : rule<create<Doc>(many(append<&Doc::items>(ref<Part>)))> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace tllrefusedglr
