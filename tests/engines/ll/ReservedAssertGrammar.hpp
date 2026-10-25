#pragma once

// A name that asserts what follows it is lexed behind a relay. The keywords
// that spell its words reserve them all the same, whichever ranks first.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tllreservedassert {
struct Item : cursiv::Extends<cursiv::AstNode> {
  bool keyword = false;
  string_view name;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Item*> items;
};

// A terminal that is a choice of keywords: not a spelling, so it ranks with the
// name and the order of declaration decides a tie.
struct Choice : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NAME : terminal<cr<"a-zA-Z_"> + many(w) + !(lit<"@"> + !lit<"@">)> {};
  struct KEYWORD : terminal<kw<"if"> | kw<"while">> {};
  struct Entry : rule<create<Item>((assign<&Item::name>(ref<NAME>) |
                                    flag<&Item::keyword>(ref<KEYWORD>)) +
                                   lit<";">)> {};
  struct Root : rule<create<Doc>(many(append<&Doc::items>(ref<Entry>)))> {};
  using entry = Root;
  using ignore = WS;
};
struct ChoiceLl : Choice {};
struct ChoiceLr : Choice {};

// The same names without the assertion: the reservation these two read.

}  // namespace tllreservedassert
