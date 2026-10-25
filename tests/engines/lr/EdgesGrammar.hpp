#pragma once

// Where a reading commits and where a token ends, each shape behind its own
// opening keyword in one grammar: no rule is shared, so each case reads as its
// grammar alone does. The regions open on a mark their text follows with no
// gap, as a document's first token has none.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

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
  struct Case : rule<(kw<"commit"> + ref<tcommit::G::Root>) |
                     (lit<"region:"> + ref<tregion::G::Root>) |
                     (kw<"guard"> + ref<tguardalt::G::Root>) |
                     (kw<"split"> + ref<tlexsplit::G::Root>)> {};
  using entry = Case;
  using ignore = WS;
};
}  // namespace tedges
