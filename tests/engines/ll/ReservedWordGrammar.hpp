#pragma once

#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"

namespace tllreserved {
struct Item : cursiv::Extends<cursiv::AstNode> {
  boolean guarded = false;
  string_view name;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Item*> items;
  boolean ruled = false;
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NAME : terminal<!(kw<"if"> | kw<"while">) + !ikw<"case"> +
                         !ikw<"loop"> + cr<"a-zA-Z_"> + many(w)> {};
  struct Entry
      : rule<create<Item>(option(flag<&Item::guarded>(kw<"if">)) +
                          assign<&Item::name>(ref<NAME>) + lit<";">)> {};
  // A rule only where the text ends on it.
  struct HR : terminal<lit<"----"> + eof> {};
  struct Root : rule<create<Doc>(many(append<&Doc::items>(ref<Entry>)) +
                                 option(flag<&Doc::ruled>(ref<HR>)))> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace tllreserved
