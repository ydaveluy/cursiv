#pragma once

// A terminal that is one keyword, or a choice of keywords, is a keyword
// whatever conditions surround it: it takes its words from a name that
// ranks first, wherever it is declared, and keeps them only where its own
// condition holds.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tkwrelay {
struct Item : cursiv::Extends<cursiv::AstNode> {
  bool keyword = false;
  string_view name;
  string_view arg;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Item*> items;
};

// An assertion behind a choice of keywords; the name is the first arm and
// declared first.
struct Choice : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NAME : terminal<cr<"a-zA-Z_"> + many(w)> {};
  struct KEYWORD : terminal<(kw<"if"> | kw<"while">)+&lit<";">> {};
  static constexpr auto Tail =
      option(assign<&Item::arg>(ref<NAME>) |
             (lit<"("> + assign<&Item::arg>(ref<NAME>) + lit<")">));
  struct Entry : rule<create<Item>((assign<&Item::name>(ref<NAME>) |
                                    flag<&Item::keyword>(ref<KEYWORD>)) +
                                   Tail + lit<";">)> {};
  struct Root : rule<create<Doc>(many(append<&Doc::items>(ref<Entry>)))> {};
  using entry = Root;
  using ignore = WS;
};

// The assertion in the rule reads tokens, so it skips blanks: the keyword
// is read whatever follows, and the rule then asks.
struct Rule : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NAME : terminal<cr<"a-zA-Z_"> + many(w)> {};
  struct KEYWORD : terminal<kw<"if"> | kw<"while">> {};
  struct Entry
      : rule<create<Item>((assign<&Item::name>(ref<NAME>) |
                           (flag<&Item::keyword>(ref<KEYWORD>) + &lit<";">)) +
                          lit<";">)> {};
  struct Root : rule<create<Doc>(many(append<&Doc::items>(ref<Entry>)))> {};
  using entry = Root;
  using ignore = WS;
};

// A literal that carries its condition ranks as a written word.
struct Written : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NAME : terminal<cr<"a-zA-Z_"> + many(w)> {};
  struct KEYWORD : terminal<lit<"if", &lit<";">>> {};
  static constexpr auto Tail =
      option(assign<&Item::arg>(ref<NAME>) |
             (lit<"("> + assign<&Item::arg>(ref<NAME>) + lit<")">));
  struct Entry : rule<create<Item>((assign<&Item::name>(ref<NAME>) |
                                    flag<&Item::keyword>(ref<KEYWORD>)) +
                                   Tail + lit<";">)> {};
  struct Root : rule<create<Doc>(many(append<&Doc::items>(ref<Entry>)))> {};
  using entry = Root;
  using ignore = WS;
};

struct Pick : cursiv::Extends<cursiv::AstNode> {
  string_view first;
  string_view second;
};
struct Picks : cursiv::Extends<cursiv::AstNode> {
  vector<Pick*> items;
};

// The branch that matches decides, whatever else the terminal holds: a
// branch behind a condition ranks as a keyword where it matches.
struct MixedHeld : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NAME : terminal<cr<"a-zA-Z_"> + many(w)> {};
  struct MIXED : terminal<ref<NAME> | ((kw<"if"> | kw<"while">)+&lit<";">)> {};
  struct Entry
      : rule<create<Pick>((assign<&Pick::first>(ref<NAME>) + lit<";">) |
                          (assign<&Pick::second>(ref<MIXED>) + lit<";">))> {};
  struct Root : rule<create<Picks>(many(append<&Picks::items>(ref<Entry>)))> {};
  using entry = Root;
  using ignore = WS;
};

// A condition adds no priority, and a literal that is not a word is not a
// keyword: both ties go to the first arm.
struct Starred : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct STAR_L : terminal<lit<"*">> {};
  struct STAR_K : terminal<kw<"*">> {};
  struct Entry
      : rule<create<Pick>((assign<&Pick::first>(ref<STAR_L>) + lit<"(">) |
                          (assign<&Pick::second>(ref<STAR_K>) + lit<"(">))> {};
  struct Root : rule<create<Picks>(many(append<&Picks::items>(ref<Entry>)))> {};
  using entry = Root;
  using ignore = WS;
};

struct Called : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NAME : terminal<cr<"a-zA-Z_"> + many(w)> {};
  struct FN_NAME : terminal<some(cr<"a-z">) + &lit<"(">> {};
  struct Entry
      : rule<create<Pick>((assign<&Pick::first>(ref<NAME>) + lit<"(">) |
                          (assign<&Pick::second>(ref<FN_NAME>) + lit<"(">))> {};
  struct Root : rule<create<Picks>(many(append<&Picks::items>(ref<Entry>)))> {};
  using entry = Root;
  using ignore = WS;
};

// Where a word byte follows the keyword and the patterns stop there, the
// keyword branch is removed and the two patterns tie: the first arm reads.
struct Narrow : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NUM : terminal<some(d)> {};
  struct LOW : terminal<some(cr<"a-z">)> {};
  struct MIXED : terminal<some(cr<"a-z">) | kw<"if">> {};
  struct Entry : rule<create<Pick>((assign<&Pick::first>(ref<LOW>) +
                                    option(ref<NUM>) + lit<";">) |
                                   (assign<&Pick::second>(ref<MIXED>) +
                                    option(ref<NUM>) + lit<";">))> {};
  struct Root : rule<create<Picks>(many(append<&Picks::items>(ref<Entry>)))> {};
  using entry = Root;
  using ignore = WS;
};

// Several conditions, and keywords of their own, after the choice.
struct SharedTwo : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NAME : terminal<cr<"a-zA-Z_"> + many(w)> {};
  struct SHARED
      : terminal<(ref<NAME> | kw<"if"> | kw<"while">)+&lit<";"> + !lit<";x">> {
  };
  struct Entry
      : rule<create<Pick>((assign<&Pick::first>(ref<NAME>) + lit<";">) |
                          (assign<&Pick::second>(ref<SHARED>) + lit<";">))> {};
  struct Root : rule<create<Picks>(many(append<&Picks::items>(ref<Entry>)))> {};
  using entry = Root;
  using ignore = WS;
};

struct Suite : Choice {
  struct Root : rule<(lit<"choice:"> + ref<Choice::Root>) |
                     (lit<"rule:"> + ref<Rule::Root>) |
                     (lit<"written:"> + ref<Written::Root>) |
                     (lit<"mixedheld:"> + ref<MixedHeld::Root>) |
                     (lit<"narrow:"> + ref<Narrow::Root>) |
                     (lit<"sharedtwo:"> + ref<SharedTwo::Root>) |
                     (lit<"starred:"> + ref<Starred::Root>) |
                     (lit<"called:"> + ref<Called::Root>)> {};
  using entry = Root;
};
struct Ll : Suite {};
struct Lr : Suite {};

}  // namespace tkwrelay
