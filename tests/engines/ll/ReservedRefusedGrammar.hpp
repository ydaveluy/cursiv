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
  using ignored = WS;
};
struct Ll : Base {};
struct Lr : Base {};

// The keyword of `Base` or a name behind nested parentheses: a prediction
// reads past them to the word the keyword refuses behind its own bytes.
struct MemoBase : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct IN : terminal<!kw<"in-place"> + kw<"in">> {};
  struct NAME : terminal<cr<"a-z"> + many(w)> {};
  struct Nest : rule<(lit<"("> + ref<Nest> + lit<")">) | lit<"()">> {};
  struct Keyed : rule<create<Item>(ref<Nest> + flag<&Item::keyword>(ref<IN>) +
                                   lit<"-"> + assign<&Item::name>(ref<NAME>))> {
  };
  struct Named : rule<create<Item>(ref<Nest> + assign<&Item::name>(ref<NAME>) +
                                   lit<"-"> + ref<NAME>)> {};
  struct Root : rule<create<Doc>(many(
                    append<&Doc::items>(ref<Keyed> | ref<Named>) + lit<".">))> {
  };
  using entry = Root;
  using ignored = WS;
};
struct MemoLl : MemoBase {};
struct MemoLr : MemoBase {};

// The keyword of `Base` and a dash that stands only before a name: whether
// the keyword reserves the name's text is read past the name.
struct CondLl : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct IN : terminal<!kw<"in-place"> + kw<"in">> {};
  struct NAME : terminal<cr<"a-z"> + many(w)> {};
  struct Part : rule<create<Item>(
                    flag<&Item::keyword>(ref<IN>) |
                    (&(lit<"-"> + ref<NAME>)+flag<&Item::dash>(lit<"-">)) |
                    assign<&Item::name>(ref<NAME>))> {};
  struct Root : rule<create<Doc>(many(append<&Doc::items>(ref<Part>)))> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace tllrefused
