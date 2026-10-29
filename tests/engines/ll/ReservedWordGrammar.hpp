#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tllreserved {
struct Item : cursiv::Extends<cursiv::AstNode> {
  bool guarded = false;
  string_view name;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Item*> items;
  bool ruled = false;
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
struct Ll : Base {};
struct Lr : Base {};
}  // namespace tllreserved
