#pragma once

// Regions that name trivia of their own, each behind its opening keyword in
// one grammar both engines read. The grammar skips blanks and keeps `#`
// comments; a newline is one of its tokens.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tskip {

namespace ast {
struct Doc : cursiv::Extends<cursiv::AstNode> {};
}  // namespace ast

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t)">)> {};
  struct NL : terminal<some(lit<"\n">)> {};
  struct CM : terminal<lit<"#"> + many(cr<"a-z">)> {};
  struct BANG : terminal<lit<"!">> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct NUM : terminal<some(d)> {};
  // Nothing skipped between the segments of a path, in a rule it calls too.
  struct Seg : rule<lit<"."> + ref<ID>> {};
  struct Path : rule<kw<"use"> + tight(ref<ID> + many(ref<Seg>) + lit<";">)> {};
  // Arguments that skip blanks again, and nothing else, inside a tight call.
  struct Args
      : rule<skip(ignored<WS>, hide<>, lit<"("> + many(ref<ID>) + lit<")">)> {};
  struct Call : rule<kw<"do"> + tight(ref<ID> + ref<Args>) +
                     kw<"end">> {};
  // ...and arguments that skip what the grammar skips.
  struct Grammar
      : rule<skip(ignored<WS>, hide<CM>, lit<"("> + many(ref<ID>) + lit<")">)> {
  };
  struct Again
      : rule<kw<"dd"> + tight(ref<ID> + ref<Grammar>) +
             kw<"end">> {};
  // A mark kept in the tree between the words.
  struct Bang : rule<kw<"hb"> + skip(ignored<WS>, hide<BANG>,
                                     ref<ID> + ref<ID> + kw<"end">)> {};
  // Comments dropped in the outer region, not read in the inner one, and
  // kept again past both.
  struct Item
      : rule<kw<"m"> +
             skip(ignored<WS, CM>, hide<>,
                  skip(ignored<WS>, hide<>, ref<ID> + lit<":">) + lit<"?">) +
             kw<"end">> {};
  // A run at the head of a region: its first sign is the caller's to reach.
  struct Neg : rule<kw<"neg"> + tight(many(lit<"-">) + ref<NUM>)> {};
  // A choice and a rule at the head of a region: the gap in front of either is
  // the caller's.
  struct Pick : rule<kw<"pick"> + tight((lit<"+"> | lit<"*">) + ref<ID>)> {};
  struct Via : rule<kw<"via"> + tight(ref<Seg> + lit<";">)> {};
  // A newline ends a line, except between parentheses; a name followed by
  // `=` takes a second token to tell from a name alone.
  struct Expr;
  struct Tuple : rule<skip(ignored<WS, NL>, hide<CM>,
                           lit<"("> + ref<Expr> + many(lit<","> + ref<Expr>) +
                               lit<")">)> {};
  struct Expr
      : rule<(ref<ID> + lit<"="> + ref<Expr>) | ref<ID> | ref<NUM> | ref<Tuple>> {
  };
  struct Line : rule<kw<"let"> + ref<ID> + lit<"="> + ref<Expr> + ref<NL>> {};
  // A blank the grammar skips, read as a token by a rule only a tight region
  // reaches.
  struct Pair : rule<ref<ID> + ref<WS> + ref<ID>> {};
  struct Band
      : rule<kw<"band"> + tight(lit<"["> + ref<Pair> + lit<"]">)> {};
  struct Root
      : rule<create<ast::Doc>(many(ref<Path> | ref<Call> | ref<Again> |
                                   ref<Bang> | ref<Item> | ref<Neg> |
                                   ref<Pick> | ref<Via> | ref<Line> |
                                   ref<Band>))> {};
  using entry = Root;
  using ignore = WS;
  using hidden = list<CM>;
};
// A condition at the tail of a region that skips newlines reads the gap in
// front of the caller's newline as the caller does.
struct Lr : Base {
  struct Tail
      : rule<kw<"tail"> +
             skip(ignored<WS, NL>, hide<CM>,
                  lit<"("> + ref<ID> + lit<")"> + !gap::line) +
             ref<NL>> {};
  struct Root
      : rule<create<ast::Doc>(many(ref<Path> | ref<Call> | ref<Again> |
                                   ref<Bang> | ref<Item> | ref<Neg> |
                                   ref<Pick> | ref<Via> | ref<Line> |
                                   ref<Band> | ref<Tail>))> {};
  using entry = Root;
};
struct Ll : Base {};

}  // namespace tskip
