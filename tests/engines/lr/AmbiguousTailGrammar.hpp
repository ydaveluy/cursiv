#pragma once

// A cell the text never settles, hit once per statement: after `p`, a `(`
// is the call's or the next statement's, and both readings stay valid to the
// end. Every reading that passes it forks; the arms must fall back together.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tambtail {

namespace ast {
struct Doc : cursiv::Extends<cursiv::AstNode> {};
}  // namespace ast

// Beside it, a cell with arms that read a line break apart: past `p a`, one
// arm stands where a NEWLINE is a token and the other where it is trivia,
// and both close the statement on `b`. Counting a shift apart, the two still
// have one future.
struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t)">)> {};
  struct NL : terminal<eol> {};
  struct NEWLINE : terminal<eol> {};
  struct Stat
      : rule<(lit<"p"> + option(lit<"("> + lit<"n"> + lit<")">))  //
             | (lit<"("> + lit<"n"> + lit<")">)                  //
             | (lit<"p"> + lit<"="> + lit<"n">)> {};
  struct P1 : rule<lit<"p">> {};
  struct P2 : rule<lit<"p">> {};
  struct X : rule<ref<P1> + lit<"a"> + many(ref<NEWLINE>) + lit<"b">> {};
  struct Y : rule<ref<P2> + lit<"a"> + lit<"b">> {};
  struct Line : rule<lit<"k"> + (ref<X> | ref<Y>) + lit<";">> {};
  struct Root : rule<create<ast::Doc>(many(ref<Stat> | ref<Line>))> {};
  using entry = Root;
  using ignored = list<WS, NL>;
};

}  // namespace tambtail
