// Lexical guards belong to individual acceptances. Keyword, word, typo and
// candidate cases share one table and keep their own modes. The competing-
// trivia policy has its own table.
#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "engines/lr/AlikeCandidatesGrammar.hpp"
#include "engines/lr/RefusedHeadWordsGrammar.hpp"
#include "engines/lr/TypoAlternativesGrammar.hpp"

namespace tguard {

struct Doc : cursiv::Extends<cursiv::AstNode> {
  string_view value;
};

// `a` twice: B unguarded, A refusing a `;` right after it. Where B wins the
// spelling, A is still the candidate its own guard admits or refuses.
namespace own {
struct G : cursiv::GrammarSpec {
  struct B : terminal<lit<"a">> {};
  struct A : terminal<lit<"a", !cr<";">>> {};
  struct Root : rule<create<Doc>((assign<&Doc::value>(ref<B>) + lit<"_">) |
                                 (assign<&Doc::value>(ref<A>) + lit<";">))> {};
};
}  // namespace own

// One terminal with a keyword alternative and a punctuation one: the word
// boundary is the keyword's alone, and `&&` ends on nothing.
namespace mixed {
struct G : cursiv::GrammarSpec {
  struct AND : terminal<kw<"and"> | lit<"&&">> {};
  struct OTHER : terminal<lit<"&&">> {};
  struct Root
      : rule<create<Doc>((assign<&Doc::value>(ref<AND>) + lit<"B">) |
                         (assign<&Doc::value>(ref<OTHER>) + lit<"C">))> {};
};
}  // namespace mixed

// `abc` twice, B guarded and declared first, so a re-reading that walks the
// acceptances in declaration order meets it first and owes its guard, which
// A's does not cover.
namespace typo {
struct G : cursiv::GrammarSpec {
  struct B : terminal<lit<"abc", !cr<";">>> {};
  struct A : terminal<lit<"abc">> {};
  struct Root : rule<create<Doc>((assign<&Doc::value>(ref<B>) + lit<";">) |
                                 (assign<&Doc::value>(ref<A>) + lit<"_">))> {};
};
}  // namespace typo

// A name whose alphabet is narrower than the grammar's word class: `x` is a
// word byte, so `abx` refuses the keyword and is the name `ab` before it.
namespace reserved {
struct G : cursiv::GrammarSpec {
  struct ID : terminal<some(cr<"ab">)> {};
  struct X : terminal<some(cr<"x">)> {};
  struct Root : rule<create<Doc>((assign<&Doc::value>(ref<ID>) + ref<X>) |
                                 (kw<"ab"> + lit<";">))> {};
};
}  // namespace reserved

// A guard refusing a word byte AND another: the reserve lifts on the first
// and stands on the second, so the answer is the byte's and not the set's.
namespace bytewise {
struct G : cursiv::GrammarSpec {
  struct A : terminal<lit<"ab", !cr<"_;">>> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Root : rule<create<Doc>((assign<&Doc::value>(ref<A>) + lit<"?">) |
                                 (kw<"ab"> + lit<"!">) |
                                 (assign<&Doc::value>(ref<ID>) + lit<"_">))> {};
};
}  // namespace bytewise

// Two names of one spelling behind a refused keyword: the position reads
// several, so the sweep is what tells them apart.
namespace rival {
struct G : cursiv::GrammarSpec {
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Other : terminal<cr<"a"> + cr<"b">> {};
  struct Root
      : rule<create<Doc>(
            (kw<"ab"> + lit<"!">) | (assign<&Doc::value>(ref<ID>) + lit<"_">) |
            (assign<&Doc::value>(ref<Other>) + lit<"_"> + lit<";">))> {};
};
}  // namespace rival

// Each literal of one terminal keeps its own class guard: `+` refuses a
// second `+`, `-` a second `-`.
namespace twoguards {
struct G : cursiv::GrammarSpec {
  struct ID : terminal<some(cr<"a-z">)> {};
  struct OP : terminal<lit<"+", !cr<"+">> | lit<"-", !cr<"-">>> {};
  struct Root : rule<create<Doc>(assign<&Doc::value>(ref<ID>) + ref<OP> +
                                 lit<";">)> {};
};
}  // namespace twoguards

// The sweep leaves exactly one candidate, and it is not the one the walk
// elected: a keyword's reserve takes the name away and the position reads the
// keyword alone.
namespace sole {
struct G : cursiv::GrammarSpec {
  struct ID : terminal<some(cr<"a-z">)> {};
  struct AND : terminal<kw<"and"> | lit<"&&">> {};
  struct Root : rule<create<Doc>((assign<&Doc::value>(ref<ID>) + lit<";">) |
                                 (assign<&Doc::value>(ref<AND>) + lit<"!">))> {
  };
};
}  // namespace sole

// One position, two extents: the parse takes `<` twice where the walk's
// longest reading is `<<`, so a caret has to offer the shorter one.
namespace shorter {
struct G : cursiv::GrammarSpec {
  struct Twin : terminal<cr<"<"> + cr<"<">> {};
  struct Root
      : rule<create<Doc>((lit<"<"> + lit<"<"> + lit<"a">) |
                         (lit<"<<"> + lit<"b">) | (ref<Twin> + lit<"x">))> {};
};
}  // namespace shorter

// Nested generics closing on `>>`, which is also a terminal of its own. The
// state inside the nest reads `>` and not `>>`, so its own alphabet holds no
// `>>` and the longest match there IS `>`: the mode is what splits the
// spelling, and no rule ever rebuilds a terminal out of shorter ones.
namespace nested {
struct G : cursiv::GrammarSpec {
  struct ID : terminal<some(cr<"a-z">)> {};
  struct SHR : terminal<lit<">>">> {};
  struct Root
      : rule<create<Doc>((assign<&Doc::value>(ref<ID>) + lit<"<"> + ref<ID> +
                          lit<"<"> + ref<ID> + lit<">"> + lit<">">) |
                         (assign<&Doc::value>(ref<ID>) + ref<SHR> + ref<ID>))> {
  };
};
}  // namespace nested

// The state's whole election is refused and nothing there stands in its
// place: the walk gives the match back to what the path accepted shorter,
// which is the only reading of these bytes left.
namespace stump {
struct G : cursiv::GrammarSpec {
  struct One : terminal<lit<"a">> {};
  struct Two : terminal<lit<"ab", !cr<";">>> {};
  struct Root : rule<create<Doc>(
                    (assign<&Doc::value>(ref<Two>) + lit<"!">) |
                    (assign<&Doc::value>(ref<One>) + lit<"b"> + lit<";">))> {};
};
}  // namespace stump

// Two guards over one spelling: the winner's refuses the byte behind, and the
// one it outranked declares a boundary of its own that admits it. The refusal
// path then has to weigh a bounded acceptance, not only an unbounded one.
namespace twin {
struct G : cursiv::GrammarSpec {
  struct P : terminal<lit<"ab", !cr<";">>> {};
  struct Q : terminal<lit<"ab", !cr<"!">>> {};
  struct Root : rule<create<Doc>((assign<&Doc::value>(ref<P>) + lit<"!">) |
                                 (assign<&Doc::value>(ref<Q>) + lit<";">))> {};
};
}  // namespace twin

// Each case lexes in modes of its own: the table only lends it its classes.
struct Cases : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Entry
      : rule<(kw<"own"> + ref<own::G::Root>) |
             (kw<"mixed"> + ref<mixed::G::Root>) |
             (kw<"typo"> + ref<typo::G::Root>) |
             (kw<"reserved"> + ref<reserved::G::Root>) |
             (kw<"bytewise"> + ref<bytewise::G::Root>) |
             (kw<"rival"> + ref<rival::G::Root>) |
             (kw<"twoguards"> + ref<twoguards::G::Root>) |
             (kw<"sole"> + ref<sole::G::Root>) |
             (kw<"shorter"> + ref<shorter::G::Root>) |
             (kw<"nested"> + ref<nested::G::Root>) |
             (kw<"stump"> + ref<stump::G::Root>) |
             (kw<"twin"> + ref<twin::G::Root>)> {};
  using entry = Entry;
  using ignore = WS;
};

// A guard that is a word, or two: `<!` stands unless `--` or `[CDATA[`
// follows it whole, and `#` unless the word `include` does -- `#includes`
// keeps it, the rescue class being the word's own boundary.
namespace deep {
struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct DTD
      : terminal<through(lit<"<!", !(lit<"--"> | lit<"[CDATA[">)>, lit<">">)> {
  };
  struct COMMENT : terminal<through(lit<"<!--">, lit<"-->">)> {};
  struct CDATA : terminal<through(lit<"<![CDATA[">, lit<"]]>">)> {};
  struct LT : terminal<lit<"<">> {};
  struct BANG : terminal<lit<"!">> {};
  struct HASH : terminal<lit<"#", !kw<"include">>> {};
  struct ID : terminal<some(cr<"a-zA-Z">)> {};
  struct Root : rule<create<Doc>(many(ref<DTD> | ref<COMMENT> | ref<CDATA> |
                                      ref<LT> | ref<BANG> | ref<HASH> |
                                      ref<ID>))> {};
  using entry = Root;
  using ignore = WS;
};
}  // namespace deep

// Words refused behind a head where the mode reads several: `##` refused
// when `include` follows it whole, beside `a` twice as in `own` -- so the
// sweep answers for the refusal too, and hands the shorter `#` back.
namespace behind {
struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct HASH : terminal<lit<"#">> {};
  struct HASHES : terminal<lit<"##", !kw<"include">>> {};
  struct LONG : terminal<lit<"$"> | lit<"$$i">> {};
  struct LONGS : terminal<lit<"$$", !kw<"include">>> {};
  struct BOUNDED : terminal<lit<"%", !cr<"b">>> {};
  struct PLAIN : terminal<lit<"%">> {};
  struct REFUSES : terminal<lit<"%b", !lit<"cd">>> {};
  struct CONJ : terminal<lit<"~">> {};
  struct CONJS : terminal<lit<"~~", !(lit<"abc"> | lit<"def">)>> {};
  struct ID : terminal<some(cr<"a-zA-Z">)> {};
  struct B : terminal<lit<"a">> {};
  struct A : terminal<lit<"a", !cr<";">>> {};
  struct Root : rule<create<Doc>(many((ref<B> + lit<"_">) | (ref<A> + lit<";">) |
                                      ref<HASH> | ref<HASHES> | ref<LONG> |
                                      ref<LONGS> | ref<BOUNDED> | ref<PLAIN> |
                                      ref<REFUSES> | ref<CONJ> | ref<CONJS> |
                                      ref<ID>))> {};
  using entry = Root;
  using ignore = WS;
};
}  // namespace behind

struct Items : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> items;
};

// A trivia declared before two tokens that spell `ab`, the spelled one
// refusing a `!` behind it: where it is refused, the name stands, not the
// trivia.
namespace trivia {
struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<" ">)> {};
  struct W : terminal<some(cr<"a-b">)> {};
  struct E : terminal<lit<"ab", !cr<"!">>> {};
  struct U : terminal<some(cr<"a-c">)> {};
  struct BANG : terminal<lit<"!">> {};
  struct Root : rule<create<Items>(
                    many(append<&Items::items>(ref<E> | ref<U> | ref<BANG>)))> {};
  using entry = Root;
  using ignore = list<WS, W>;
};
}  // namespace trivia

// A refused word of two bytes behind a head of three: `def;;` is no `B`,
// however a misspelling reads it.
namespace typomulti {
struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct B : terminal<cr<"d"> + lit<"ef", !lit<";;">>> {};
  struct Root
      : rule<create<Doc>(assign<&Doc::value>(ref<B>) + lit<";"> + lit<";">)> {};
  using entry = Root;
  using ignore = WS;
};
}  // namespace typomulti

struct G : Cases {
  struct Entry : rule<ref<Cases::Entry> |
                      (kw<"deep"> + ref<deep::G::Root>) |
                      (kw<"behind"> + ref<behind::G::Root>) |
                      (kw<"typomulti"> + ref<typomulti::G::Root>) |
                      ref<ttyporead::G::Root> |
                      (kw<"heads"> + ref<trefusedheads::G::Root>) |
                      (kw<"shifted"> + ref<talike::Shifted::Root>)> {};
  using entry = Entry;
};

}  // namespace tguard
