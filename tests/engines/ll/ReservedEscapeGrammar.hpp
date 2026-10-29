#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tllescape {
struct Item : cursiv::Extends<cursiv::AstNode> {
  bool guarded = false;
  string_view name;
  string_view label;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Item*> items;
};

// A name whose escape the word boundary holds, and a label whose colon it
// does not: past a refused word, the name reads on, the label stops.
struct Base : cursiv::GrammarSpec {
  static constexpr auto word_boundary = cr<R"(a-zA-Z0-9_\\)">;
  struct WS : terminal<some(s)> {};
  static constexpr auto Esc = lit<"\\u"> + repeat<4>(cr<"0-9a-fA-F">);
  struct NAME
      : terminal<!kw<"in"> + (cr<"a-z"> | Esc) + many(cr<"a-z0-9"> | Esc)> {};
  struct LABEL : terminal<!kw<"in"> + cr<"a-z"> + many(cr<"a-z">) +
                          lit<":">> {};
  struct Entry
      : rule<create<Item>((assign<&Item::label>(ref<LABEL>) |
                           (option(flag<&Item::guarded>(kw<"in">)) +
                            assign<&Item::name>(ref<NAME>))) +
                          lit<";">)> {};
  struct Root : rule<create<Doc>(many(append<&Doc::items>(ref<Entry>)))> {};
  using entry = Root;
  using ignored = WS;
};
struct Ll : Base {};
}  // namespace tllescape
