#pragma once

// Guards that decide where a token ends, on both engines: word boundaries,
// passable refusals, and assertions that let a longer token read over a
// shorter one.

#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"

namespace tlexguards {

struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> items;
};

struct Link : cursiv::Extends<cursiv::AstNode> {
  string_view from;
  string_view to;
};
struct Links : cursiv::Extends<cursiv::AstNode> {
  vector<Link*> links;
};

struct Main : cursiv::GrammarSpec {
  // Every character but a blank or `;` continues a word.
  static constexpr auto word_boundary = ~cr<" ;">;
  struct WS : terminal<some(cr<" ">)> {};
  // The keyword ends before a blank or `;`, the literal before anything else:
  // two tokens, whichever the grammar names first.
  struct Ended : rule<kw<"go"> + lit<";">> {};
  struct Running : rule<lit<"go", !cr<" ;">> + lit<"!">> {};
  // `if` is refused behind `x` unless a letter follows it. A match may end on
  // `if`, or read one more letter where `λ` does not follow.
  struct WORD : terminal<lit<"x"> + !lit<"if", !cr<"a-z">> + cr<"a-z"> +
                         cr<"a-z"> + option(cr<"a-z"> + !cr<"λ">)> {};
  struct TAIL : terminal<cr<"a-z"> + cr<"λ">> {};
  // More scalars of one width than a recognizer tests one by one, none next to
  // another: `u` stands before one of them, `v` before `q` where none follows.
  static constexpr auto Scalars =
      cr<"αγεηιμοςυχωБГЕЗЙЛНПСУХЧЩЫЭЯбгезйлнпсух">;
  struct CHOSEN : terminal<lit<"u"> + &Scalars + some(cr<"^ ;">)> {};
  struct UNCHOSEN
      : terminal<lit<"v"> + &(lit<"q"> + !Scalars) + some(cr<"^ ;">)> {};
  // As many words, each one let pass by a letter behind it.
  template <cursiv::str W>
  static constexpr auto Passable = lit<W, !cr<"a-z">>;
  struct UNWORDED
      : terminal<lit<"w"> +
                 &(lit<"q"> +
                   !(Passable<"a0"> | Passable<"a1"> | Passable<"a2"> |
                     Passable<"a3"> | Passable<"a4"> | Passable<"a5"> |
                     Passable<"a6"> | Passable<"a7"> | Passable<"a8"> |
                     Passable<"a9"> | Passable<"b0"> | Passable<"b1"> |
                     Passable<"b2"> | Passable<"b3"> | Passable<"b4"> |
                     Passable<"b5"> | Passable<"b6"> | Passable<"b7"> |
                     Passable<"b8"> | Passable<"b9"> | Passable<"c0"> |
                     Passable<"c1"> | Passable<"c2"> | Passable<"c3"> |
                     Passable<"c4"> | Passable<"c5"> | Passable<"c6"> |
                     Passable<"c7"> | Passable<"c8"> | Passable<"c9"> |
                     Passable<"d0"> | Passable<"d1"> | Passable<"d2">)) +
                 some(cr<"^ ;">)> {};
  struct Root : rule<create<Doc>(many(append<&Doc::items>(
                    ref<Ended> | ref<Running> | ref<WORD> | ref<TAIL> |
                    ref<CHOSEN> | ref<UNCHOSEN> | ref<UNWORDED>)))> {};
  using entry = Root;
  using ignored = WS;
};
struct Lr : Main {};
struct Ll : Main {};

// A name ends before `->` or `=>` and goes on before `-` or `=`. `->` stands
// only before a letter, and `=>` asserts its `>` before reading it.
struct Overtake : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<" ">)> {};
  struct NAME : terminal<some(cr<"a-z">)> {};
  struct ARROW : terminal<lit<"->", &cr<"a-z">>> {};
  struct FAT : terminal<lit<"="> + &lit<">"> + lit<">">> {};
  struct Step : rule<create<Link>(assign<&Link::from>(ref<NAME>) +
                                  option((lit<"-"> | lit<"=">) + lit<">"> +
                                         assign<&Link::to>(ref<NAME>)))> {};
  struct Root
      : rule<create<Links>((kw<"arrow"> + append<&Links::links>(ref<Step>) +
                            ref<ARROW> + append<&Links::links>(ref<Step>)) |
                           (kw<"fat"> + append<&Links::links>(ref<Step>) +
                            ref<FAT> + append<&Links::links>(ref<Step>)) |
                           (kw<"pair"> + append<&Links::links>(ref<Step>) +
                            lit<";">))> {};
  using entry = Root;
  using ignored = WS;
};
struct OvertakeLr : Overtake {};
struct OvertakeLl : Overtake {};

// A token that starts with a blank behind an assertion: the blanks in front
// of `!` are its own, not an ignored run crossed ahead of the token.
struct BlankHead : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<" ">)> {};
  struct NAME : terminal<some(cr<"a-z">)> {};
  struct BANG : terminal<&lit<" "> + some(cr<" ">) + lit<"!">> {};
  struct Root
      : rule<create<Doc>(many(append<&Doc::items>(ref<NAME> | ref<BANG>)))> {};
  using entry = Root;
  using ignored = WS;
};
struct BlankHeadLr : BlankHead {};
struct BlankHeadLl : BlankHead {};

}  // namespace tlexguards
