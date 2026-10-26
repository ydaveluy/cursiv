#pragma once

// Where a reading commits and where a token ends, each shape behind its own
// opening keyword in one grammar: no rule is shared, so each case reads as its
// grammar alone does. The regions open on a mark their text follows with no
// gap, as a document's first token has none.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "engines/lr/GuardedChoiceGrammar.hpp"
#include "engines/lr/PeekCaretGrammar.hpp"

// Completion through ignored blanks, continuations and line comments.
namespace tcarettrivia {

struct Doc : cursiv::Extends<cursiv::AstNode> {};

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t)"> | (lit<"\\"> + eol))> {};
  struct NL : terminal<eol> {};
  struct COMMENT : terminal<lit<"//"> + many(cr<R"(^\r\n)">)> {};
  struct Root : rule<create<Doc>(
                    many(kw<"go"> + (kw<"north"> | kw<"south">)+lit<";">))> {};
  using entry = Root;
  using ignore = list<WS, NL, COMMENT>;
};

}  // namespace tcarettrivia

// Empty guarded reductions, including jumps between emitted state groups.
namespace tgroupguard {

namespace ast {
struct Doc : cursiv::Extends<cursiv::AstNode> {};
}  // namespace ast

namespace small {
struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Bang : rule<option(lit<"x">) + !lit<"z">> {};
  struct Item
      : rule<lit<"p"> + tight(lit<"a"> + &lit<"x"> + ref<Bang>) + lit<"c">> {};
  struct Root : rule<create<ast::Doc>(ref<Item>)> {};
  using entry = Root;
  using ignore = WS;
};
}  // namespace small

namespace landing {
struct E : cursiv::Extends<cursiv::AstNode> {};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  E* e = nullptr;
};

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct B : rule<create<E>> {};
  struct C : rule<ref<B> + lit<"y">> {};
  struct Root : rule<create<Doc>(lit<"p"> + &lit<"y"> +
                                 assign<&Doc::e>(ref<C>) + lit<"x">)> {};
  using entry = Root;
  using ignore = WS;
};
}  // namespace landing

// Both empty reductions share one generated table, split into small groups.
struct G : small::G {
  struct Case
      : rule<ref<small::G::Root> | (kw<"landing"> + ref<landing::G::Root>) |
             ref<tguardedchoice::Named::Root>> {};
  using entry = Case;
};

}  // namespace tgroupguard

// A refused lexer winner leaves the lower-ranked candidate available.
namespace tfallback {

namespace ast {
struct Doc : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
}  // namespace ast

struct G : cursiv::GrammarSpec {
  // Outranks ID on "a", and refuses the very byte that makes ID the reading.
  struct A : terminal<lit<"a"> + !lit<"_">> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct WS : terminal<some(s)> {};
  struct Keyword : terminal<kw<"a"> | kw<"b">> {};
  struct Plain : terminal<lit<"a"> | lit<"b">> {};
  struct Word : rule<lit<"["> +
                     ((assign<&ast::Doc::name>(ref<Keyword>) + lit<"x">) |
                      (assign<&ast::Doc::name>(ref<Plain>) + lit<"x">)) +
                     lit<"]">> {};
  struct Tail : rule<(assign<&ast::Doc::name>(ref<A>) + lit<";">) |
                     (assign<&ast::Doc::name>(ref<ID>) + lit<"_">)> {};
  struct Ahead : rule<lit<"x"> + lit<"q"> + ref<Tail>> {};
  struct Root : rule<ref<Tail> | create<ast::Doc>(lit<"x"> + lit<"z">) |
                     ref<Ahead> | ref<Word>> {};
  using entry = Root;
  using ignore = WS;
};

}  // namespace tfallback

// A cell no bounded look-ahead settles keeps both readings.
namespace tunsettled {

namespace ast {
struct P0 : cursiv::Extends<cursiv::AstNode> {};
struct P1 : cursiv::Extends<cursiv::AstNode> {};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  P0* p0 = nullptr;
  P1* p1 = nullptr;
};
}  // namespace ast

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct C : rule<lit<"x">> {};
  struct D : rule<lit<"x"> + many(lit<"a">) + lit<"c">> {};
  // The cell on a record of two tokens: which rule the record reduces to is
  // read by the look-ahead from the text past it, or left to a branch.
  struct A0 : rule<create<ast::P0>(lit<"p"> + lit<"x">)> {};
  struct A1 : rule<create<ast::P1>(lit<"p"> + lit<"x">)> {};
  struct Root
      : rule<create<ast::Doc>(
            (ref<C> + many(lit<"a">) + lit<"b">) | ref<D> |
            (assign<&ast::Doc::p0>(ref<A0>) + many(lit<"a">) + lit<"o0">) |
            (assign<&ast::Doc::p1>(ref<A1>) + many(lit<"a">) + lit<"o1">))> {};
  using entry = Root;
  using ignore = WS;
};

}  // namespace tunsettled

// Completion returns through an unrecorded caller and a repaired token.
namespace tpeekcaret {
struct Splice : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Tail : terminal<some(lit<"c">) + lit<"d">> {};
  struct Merged : terminal<lit<"ab"> + some(lit<"c">) + lit<"d">> {};
  struct Guarded : terminal<lit<"ab"> + !lit<"c">> {};
  struct BeforeB : terminal<lit<"a"> + !lit<"b">> {};
  struct SameWidth : terminal<lit<"a">> {};
  // The same two-symbol reduction in different callers. Pair writes no
  // record: completion cannot recover this return by walking CST closes.
  struct PairHead : terminal<lit<"a">> {};
  struct PairTail : terminal<lit<"b">> {};
  struct Pair : rule<ref<PairHead> + ref<PairTail>> {};
  struct Root
      : rule<create<Doc>(
            (lit<"("> + lit<"foo"> + lit<")">) |
            (lit<"{"> + lit<"a"> + option(lit<"b">) + lit<"c"> + lit<"}">) |
            (lit<"{"> + ref<Guarded> + lit<"!"> + lit<"}">) |
            (lit<"["> + lit<"a"> + option(lit<"b">) + ref<Tail> + lit<"]">) |
            (lit<"["> + ref<Merged> + lit<"!"> + lit<"]">) |
            (lit<"<"> + ref<BeforeB> + option(lit<"b">) + lit<"c"> + lit<">">) |
            (lit<"<"> + ref<SameWidth> + lit<"d"> + lit<">">) |
            (lit<"x"> + ref<Pair> + lit<"!">) |
            (lit<"y"> + ref<Pair> + lit<"?">) |
            (lit<"u"> + many(lit<"("> + lit<"a"> + lit<"b"> + lit<")">) +
             lit<"!"> + option(lit<"next">)))> {};
  using entry = Root;
  using ignore = WS;
};
}  // namespace tpeekcaret

namespace tedges {
struct Words : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
};
}  // namespace tedges

// Commits: a quantifier stops only where its prefix cannot be read, whatever
// opens the text that follows it.
namespace tcommit {
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> words;
};
struct G : tedges::Words {
  struct A : terminal<lit<"a">> {};
  // A block with no end, its items opening on either of two tokens: nothing
  // else may follow it, so each next item is the innermost block's.
  struct Block
      : rule<lit<"="> + lit<"r"> +
             option(lit<"t"> + some(commit(lit<"j"> | lit<"l">, ref<Block>)))> {
  };
  struct Root : rule<create<Doc>(
      (lit<"s"> + option(commit(lit<"a">, lit<"b">)) + lit<"a">) |
      (lit<"q"> + option(commit(lit<"a">, lit<"b">)) + lit<"a"> + lit<"b">) |
      (lit<"e"> + option(commit(option(lit<"x">) + lit<"a">, lit<"b">)) +
       lit<"a">) |
      (lit<"h"> + many(commit(lit<",">, append<&Doc::words>(ref<A>))) +
       lit<"b">) |
      (lit<"k"> + some(commit(lit<"j"> | lit<"l">, ref<Block>))) |
      (lit<"["> + tight(option(commit(lit<"a">, lit<"b">)) + lit<"a">) +
       lit<"]">))> {};
};
}  // namespace tcommit

// Regions whose edges are not a literal at the head of the alternative: the
// boundary before a region belongs to the caller wherever the region stands,
// and a refusal in a rule holds inside it.
namespace tregion {

namespace ast {
struct Item : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Item*> items;
};
}  // namespace ast

struct G : tedges::Words {
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Inner : rule<assign<&ast::Item::name>(ref<ID>)> {};
  struct Name : rule<!kw<"state"> + assign<&ast::Item::name>(ref<ID>)> {};
  // The region opens with a rule.
  struct Bracket : rule<tight(ref<Inner> + lit<"]">)> {};
  // The region stands past the head of the alternative.
  struct Mid : rule<create<ast::Item>(
                   lit<"d"> + tight(lit<"e"> + lit<"f">) +
                   assign<&ast::Item::name>(ref<ID>))> {};
  // The region's head is optional.
  struct OptHead
      : rule<create<ast::Item>(
            lit<"o"> + tight(option(lit<"a">) + lit<"b">) +
            assign<&ast::Item::name>(ref<ID>))> {};
  // A refusal at either edge reads the region's own bytes: at the tail, a
  // blank the tight mode reads no token of.
  struct EmptyEdges
      : rule<create<ast::Item>(
            lit<"p"> +
            tight(repeat<1>(!lit<"z">) + lit<"a"> + lit<"b"> +
                  repeat<1>(!lit<"z">)) +
            lit<"c">)> {};
  // A refusal, inside a region and outside one.
  struct Angle : rule<tight(lit<"<"> + ref<Name> + lit<">">)> {};
  struct Paren : rule<lit<"("> + ref<Name> + lit<")">> {};
  // The region closes on a rule, directly or through another one.
  struct Pair : rule<lit<"+"> + lit<"-">> {};
  struct Signed : rule<lit<"*"> + ref<Pair>> {};
  struct Closing : rule<create<ast::Item>(
                       lit<"@"> + tight(lit<"["> + ref<Signed>) +
                       assign<&ast::Item::name>(ref<ID>))> {};
  struct Whole
      : rule<create<ast::Item>(lit<"#"> + tight(ref<Pair>) +
                               assign<&ast::Item::name>(ref<ID>))> {};
  struct Root : rule<create<ast::Doc>(some(append<&ast::Doc::items>(
                    ref<Bracket> | ref<Mid> | ref<OptHead> | ref<Angle> |
                    ref<Paren> | ref<EmptyEdges> | ref<Closing> |
                    ref<Whole>)))> {};
  using entry = Root;
  using ignore = WS;
};

}  // namespace tregion

// A literal's guard is the literal's own: one alternative of a terminal ends
// on the word class while its sibling ends on nothing, a guard spelled inside
// a body holds right after its bytes, and a guard on a repetition is read at
// every position of the run it closes.
namespace tguardalt {

namespace ast {
struct Pair : cursiv::Extends<cursiv::AstNode> {
  string_view left;
  string_view right;
};
}  // namespace ast

struct G : tedges::Words {
  struct UPPER : terminal<some(cr<"A-Z">)> {};
  struct AND : terminal<kw<"and"> | lit<"&&">> {};
  struct ATEND : terminal<lit<"@"> + kw<"end"> + many(d)> {};
  // Accepting and bounded: it admits inside its own run and refuses at the
  // end of it, so the acceptance is not the run's last position.
  struct RUN : terminal<some(lit<"a">) + !lit<"b">> {};
  struct Both
      : rule<create<ast::Pair>(assign<&ast::Pair::left>(ref<UPPER>) + ref<AND> +
                               assign<&ast::Pair::right>(ref<UPPER>))> {};
  struct Marked
      : rule<create<ast::Pair>(assign<&ast::Pair::left>(ref<ATEND>))> {};
  struct Runs : rule<create<ast::Pair>(assign<&ast::Pair::left>(ref<RUN>) +
                                       lit<"ab">)> {};
  struct Root : rule<ref<Both> | ref<Marked> | ref<Runs>> {};
};

}  // namespace tguardalt

// Two terminals match the same byte, and what tells them apart lies past a
// repetition nothing bounds: no budget reaches it. A position no look-ahead
// settles is split, never guessed, and the next in the same state at once.
namespace tlexsplit {

namespace ast {
struct Doc : cursiv::Extends<cursiv::AstNode> {};
}  // namespace ast

struct G : tedges::Words {
  struct X : terminal<cr<"x">> {};
  struct XY : terminal<cr<"x-y">> {};
  struct Item : rule<(ref<X> + many(lit<"a">) + lit<"b">) |
                     (ref<XY> + many(lit<"a">) + lit<"c">)> {};
  struct Root : rule<create<ast::Doc>(some(ref<Item>))> {};
};

}  // namespace tlexsplit

namespace tedges {
struct G : Words {
  struct Case
      : rule<(kw<"commit"> + ref<tcommit::G::Root>) |
             (lit<"region:"> + ref<tregion::G::Root>) |
             (kw<"guard"> + ref<tguardalt::G::Root>) |
             (kw<"split"> + ref<tlexsplit::G::Root>) |
             (lit<"cell:"> + ref<tunsettled::G::Root>) |
             (lit<"fallback:"> + ref<tfallback::G::Root>) |
             (lit<"groups:"> + ref<tgroupguard::G::Case>) |
             (lit<"splice:"> + ref<tpeekcaret::Splice::Root>) |
             skip(ignored<tcarettrivia::G::WS, tcarettrivia::G::NL,
                          tcarettrivia::G::COMMENT>,
                  hide<>, lit<"trivia:"> + ref<tcarettrivia::G::Root> + eof)> {
  };
  using entry = Case;
  using ignore = WS;
};
}  // namespace tedges
