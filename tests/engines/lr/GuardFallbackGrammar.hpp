#pragma once

// A terminal whose guard refuses at the cursor, where a lower-ranked one
// still matches: the walk keeps one acceptance per state, so the fallback
// lives only in the candidate sweep, and a refusal is not a lexical failure.
// Twice over: the drive's own lexer reads the entry, and a cell left open
// behind `x q` sends the look-ahead's reader over the same bytes.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

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
