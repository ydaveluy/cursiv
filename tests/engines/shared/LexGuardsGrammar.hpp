#pragma once

// Guards that decide where a token ends, on both engines: a keyword under a
// complemented word boundary, a refusal rescued past a terminal's head,
// assertions that decide whether a longer token reads over a shorter one, and
// one that starts a token on a blank.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

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
  struct Root : rule<create<Doc>(many(append<&Doc::items>(
                    ref<Ended> | ref<Running> | ref<WORD> | ref<TAIL>)))> {};
  using entry = Root;
  using ignored = WS;
};
struct Lr : Main {};
struct Ll : Main {};

// Assertions that decide whether a longer token reads over a shorter one. A
// name reduces where it stands before `->` or `=>`, and goes on where `-` or
// `=` follows it: the state after a name serves both callers. `->` stands
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
