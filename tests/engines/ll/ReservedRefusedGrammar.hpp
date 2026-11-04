#pragma once

#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"

namespace tllrefused {
struct Item : cursiv::Extends<cursiv::AstNode> {
  bool keyword = false;
  bool dash = false;
  string_view name;
  vector<Item*> kids;
};
struct OtherItem : cursiv::Extends<Item> {};
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
  // A literal a merged state flags, tied with a trivia: the state behind the name
  // serves a caller that takes `#x`, and one where it is a comment.
  struct Hash : terminal<lit<"#"> + many(w)> {};
  struct Value : rule<ref<NAME>> {};
  struct Marked : rule<create<Item>(lit<"marked:"> + ref<Value> + lit<"#x"> +
                                    lit<";">)> {};
  struct Plain
      : rule<create<Item>(lit<"plain:"> + ref<Value> + lit<";">)> {};
  // Two arms told apart past the word that refuses or bounds the head: a caret
  // behind that word reads the arm the parse reads.
  struct Bare : terminal<kw<"bare">> {};
  struct Head : rule<create<Item>(
                    lit<"head:"> +
                    ((lit<"z:"> + ref<IN> + lit<"-"> + ref<NAME> + lit<"a!">) |
                     (lit<"z:"> + ref<NAME> + lit<"-"> + ref<NAME> + lit<"b!">) |
                     (lit<"y:"> + ref<Bare> + lit<"-"> + lit<"a!">) |
                     (lit<"y:"> + ref<NAME> + lit<"-"> + lit<"b!">)))> {};
  // A dangling `else`: the mark goes to the deepest level that stands open.
  struct Dangling : rule<create<Item>(lit<"?"> +
                                      option(append<&Item::kids>(ref<Dangling>)) +
                                      option(flag<&Item::dash>(lit<":">)))> {};
  struct Pending : rule<lit<"pending:"> + ref<Dangling>> {};
  // A continuation that begins as a run of blanks and ends behind a byte that is
  // a token of its own, read where a state takes a one-byte token straight off
  // its first byte.
  struct SpBlank : terminal<some(s)> {};
  struct SpMore : terminal<some(cr<R"( \t)">) + lit<"_"> + lit<"\n">> {};
  struct SpWord : terminal<some(cr<"a-z">)> {};
  struct SpItem : rule<create<Item>(assign<&Item::name>(ref<SpWord>) |
                                    flag<&Item::dash>(lit<"_">) |
                                    flag<&Item::keyword>(lit<";">))> {};
  struct Spliced : rule<create<Item>(
                       lit<"sp:"> + skip(ignore<SpBlank, SpMore>, hide<>,
                                         many(append<&Item::kids>(ref<SpItem>)) +
                                             lit<".">))> {};
  // A token only one caller takes, which a comment spells: past a choice that
  // reads ahead, an option and a loop, the other caller reads the comment.
  struct Sharp : terminal<lit<"//"> + many(cr<"^\r\n">)> {};
  struct Ahead : rule<ref<NAME> | (ref<NAME> + lit<","> + lit<";">)> {};
  struct Maybe : rule<option(lit<"+"> + lit<";">)> {};
  struct Turns : rule<lit<"~"> + many(lit<"*">)> {};
  struct Spelt : rule<create<Item>(
                     lit<"spelt:"> +
                     ((lit<"<1>"> + ref<Ahead> + lit<","> + lit<".">) |
                      (lit<"<2>"> + ref<Ahead> + lit<"//x"> + lit<"!">) |
                      (lit<"<3>"> + ref<Maybe> + lit<","> + lit<".">) |
                      (lit<"<4>"> + ref<Maybe> + lit<"//x"> + lit<"!">) |
                      (lit<"<5>"> + ref<Turns> + lit<","> + lit<".">) |
                      (lit<"<6>"> + ref<Turns> + lit<"//x"> + lit<"!">)))> {};
  // Arms no token tells apart behind a spelling that holds another's.
  struct Same1 : rule<create<Item>(ref<NAME> + lit<"%^&"> + many(lit<",">))> {};
  struct Same2 : rule<create<OtherItem>(ref<NAME> + lit<"%^&"> + many(lit<",">))> {};
  struct Inside : rule<lit<"inside:"> + (create<Item>(ref<NAME> + lit<"%"> + lit<"!">) |
                                         ref<Same1> | ref<Same2>)> {};
  struct Root : rule<create<Doc>(many(append<&Doc::items>(
                    ref<Spliced> | ref<Spelt> | ref<Inside> | ref<Part> |
                    ref<Rivals> | ref<Glued> | ref<Cell> | ref<Note> | ref<Marked> |
                    ref<Plain> | ref<Head> | ref<Pending>)))> {};
  using entry = Root;
  using ignored = list<WS, Sharp>;
  using hidden = list<Hushed, Hash>;
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
