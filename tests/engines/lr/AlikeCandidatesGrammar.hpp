// Two name terminals read one word where a description may stand before
// either. Where the state past the description answers both by one reduce the
// lexer elects, and no candidate is carried; where a state shifts each, the
// next token decides and the table says so.
#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace talike {

struct Item : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  string_view note;
  bool value = false;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Item*> items;
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct TEXT : terminal<lit<"\""> + many(cr<"^\"">) + lit<"\""> > {};
  struct NAME : terminal<cr<"a-z"> + many(cr<"a-z0-9">)> {};
  struct VALUE
      : terminal<!(kw<"yes"> | kw<"no">) + cr<"a-z"> + many(cr<"a-z0-9">)> {};
  struct Note : rule<ref<TEXT>> {};
};

struct Reduced : Base {
  struct Field
      : rule<create<Item>(option(assign<&Item::note>(ref<Note>)) +
                          assign<&Item::name>(ref<NAME>) + lit<":">)> {};
  struct Choice
      : rule<create<Item>(option(assign<&Item::note>(ref<Note>)) +
                          assign<&Item::name>(ref<VALUE>))> {};
  struct Root : rule<create<Doc>(kw<"type"> +
                                 many(append<&Doc::items>(ref<Field>)) +
                                 kw<"enum"> +
                                 many(append<&Doc::items>(ref<Choice>)))> {};
  using entry = Root;
  using ignored = WS;
};

struct Shifted : Base {
  struct Mixed
      : rule<create<Item>((assign<&Item::name>(ref<NAME>) + lit<":">) |
                          (assign<&Item::name>(ref<VALUE>) +
                           flag<&Item::value>(lit<"=">)))> {};
  struct Root : rule<create<Doc>(many(append<&Doc::items>(ref<Mixed>)))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace talike
