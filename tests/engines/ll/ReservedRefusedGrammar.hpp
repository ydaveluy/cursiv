#pragma once

#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"

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
  // A keyword its gap may refuse, over two terminals that tie behind it: what
  // refuses it leaves both, and the parse goes on with the one it can take.
  struct One : terminal<cr<"X">> {};
  struct Two : terminal<cr<"X">> {};
  struct Three : terminal<cr<"X">> {};
  struct Rivals : rule<create<Item>(
                      lit<"rivals:"> +
                      ((gap::none + kw<"X"> + lit<"q">) |
                       (gap::none + ref<Three> + lit<"z">) |
                       (ref<One> + many(lit<"a">) + lit<"b">) |
                       (ref<Two> + many(lit<"a">) + lit<"c">)))> {};
  // The same, glued to what follows: the reading of a word a caret stands
  // behind is one of those that tie.
  struct Four : terminal<cr<"W">> {};
  struct Five : terminal<cr<"W">> {};
  struct Glued : rule<create<Item>(lit<"glued:"> +
                                   ((gap::none + kw<"W"> + lit<"q">) |
                                    (ref<Four> + lit<"!">) |
                                    (ref<Five> + lit<"?">)))> {};
  // A keyword two readings refuse from one cell, each past a rule that can be
  // empty: what they leave still reads.
  struct Six : terminal<cr<"V">> {};
  struct Seven : terminal<cr<"V">> {};
  struct E1 : rule<many(lit<"e1">)> {};
  struct E2 : rule<many(lit<"e2">)> {};
  struct Cell : rule<create<Item>(lit<"cell:"> +
                                  ((ref<E1> + gap::none + kw<"V"> + lit<"q">) |
                                   (ref<E2> + gap::none + kw<"V"> + lit<"r">) |
                                   (ref<Six> + lit<"!">) |
                                   (ref<Seven> + lit<"?">)))> {};
  // A keyword refused where a trivia ties with it: the trivia is crossed.
  struct Hushed : terminal<cr<"N">> {};
  struct Note : rule<create<Item>(lit<"note:"> +
                                  ((gap::none + kw<"N"> + lit<"q">) |
                                   (lit<"k"> + lit<"m">)))> {};
  struct Root : rule<create<Doc>(many(append<&Doc::items>(
                    ref<Part> | ref<Rivals> | ref<Glued> | ref<Cell> |
                    ref<Note>)))> {};
  using entry = Root;
  using ignored = WS;
  using hidden = list<Hushed>;
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
