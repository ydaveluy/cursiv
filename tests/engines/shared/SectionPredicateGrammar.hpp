// Grammars whose predicates read tokens, alone and as the section a host
// closes with `>`: the text a predicate sees ends where the section does.
#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tsecpred {

namespace ast {
struct Item : cursiv::Extends<cursiv::AstNode> {
  string_view text;
};
struct Marked : cursiv::Extends<Item> {};
struct Items : cursiv::Extends<cursiv::AstNode> {
  vector<Item*> items;
};
struct Block : cursiv::Extends<cursiv::AstNode> {
  Items* items = nullptr;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Block*> blocks;
};
}  // namespace ast

// The last word is the one whose token is followed by the end of the text.
struct Last : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<" ">)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  // This token is in the section's alphabet, even at the small guards over
  // ID below. It may cross their horizon, including a `>` inside its body.
  struct LONG : terminal<cr<"a-z"> + many(cr<"a-z >">) + lit<"!">> {};
  struct Mid
      : rule<create<ast::Item>(!(ref<ID> + eof) +
                               assign<&ast::Item::text>(ref<ID>))> {};
  struct Fin
      : rule<create<ast::Marked>(&(ref<ID> + eof) +
                                 assign<&ast::Item::text>(ref<ID>))> {};
  struct Root
      : rule<create<ast::Items>((&(ref<LONG> + eof) + ref<LONG>) |
                                (many(append<&ast::Items::items>(ref<Mid>)) +
                                 append<&ast::Items::items>(ref<Fin>)))> {};
  using entry = Root;
  using ignored = WS;
};

// A word is marked where a `>` follows it, which only the text past a section
// can say.
struct Reach : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<" ">)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Mark
      : rule<create<ast::Marked>(&(ref<ID> + lit<">">) +
                                 assign<&ast::Item::text>(ref<ID>))> {};
  struct Plain : rule<create<ast::Item>(assign<&ast::Item::text>(ref<ID>))> {};
  struct Root : rule<create<ast::Items>(
                    many(append<&ast::Items::items>(ref<Mark> | ref<Plain>)))> {
  };
  using entry = Root;
  using ignored = WS;
};

template <class In>
struct Host : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<" ">)> {};
  struct Block : rule<create<ast::Block>(assign<&ast::Block::items>(
                     section<In>(lit<"<">, lit<">">)))> {};
  struct Root
      : rule<create<ast::Doc>(many(append<&ast::Doc::blocks>(ref<Block>)))> {};
  using entry = Root;
  using ignored = WS;
};

struct Suite : Last {
  struct Root : rule<ref<Last::Root> |
                     (lit<"reach:"> + ref<Reach::Root>) |
                     (lit<"inlast:"> + ref<Host<Last>::Root>) |
                     (lit<"inreach:"> + ref<Host<Reach>::Root>)> {};
  using entry = Root;
};
struct Ll : Suite {};
struct Lr : Suite {};

}  // namespace tsecpred
