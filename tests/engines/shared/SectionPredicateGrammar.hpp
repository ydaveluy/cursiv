// Grammars whose predicates read tokens, alone and as the section a host
// closes with `>`: the text a predicate sees ends where the section does.
#pragma once

#include <cstdint>
#include <string_view>

#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"

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
struct Values : cursiv::Extends<cursiv::AstNode> {
  int64 number = -1;
  vector<int64> numbers;
  Item* node = nullptr;
  vector<Item*> nodes;
  string_view text;
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

// Tokens that stay open in a section, read up to the token that closes it.
struct Quoted : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<" ">)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct STR : terminal<through(lit<"\"">, lit<"\"">)> {};
  struct NOTE : terminal<through(lit<"/*">, lit<"*/">)> {};
  struct Item : rule<create<ast::Item>(assign<&ast::Item::text>(ref<ID>) |
                                       assign<&ast::Item::text>(ref<STR>))> {};
  struct Root : rule<create<ast::Items>(
                    many(append<&ast::Items::items>(ref<Item>)))> {};
  using entry = Root;
  using ignored = WS;
  using hidden = NOTE;
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

struct Number : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct INTEGER : terminal<as<std::int64_t>(some(d))> {};
  struct Root : rule<ref<INTEGER>> {};
  using entry = Root;
  using ignored = WS;
};

struct Name : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Root : rule<assign<&ast::Item::text>(ref<ID>)> {};
  using entry = Root;
  using ignored = WS;
};

// Section operands pay their entry's value through choices and scopes. Data
// rules still read their whole text, including the section's delimiters.
struct SectionValues : Last {
  static constexpr auto number = section<Number>(lit<"[">, lit<"]">);
  static constexpr auto node = section<Name>(lit<"(">, lit<")">);
  static constexpr auto seven = as<std::int64_t{7}>(lit<"!">);
  static constexpr auto length = [](std::string_view text) {
    return static_cast<std::int64_t>(text.size());
  };
  struct Direct : rule<assign<&ast::Values::number>(number)> {};
  struct Tight : rule<assign<&ast::Values::number>(tight(number))> {};
  struct Scoped
      : rule<assign<&ast::Values::number>(skip(ignore<WS>, hide<>, number))> {};
  struct Chosen : rule<assign<&ast::Values::number>(number | seven)> {};
  struct Nested : rule<assign<&ast::Values::number>(
                      tight(skip(ignore<WS>, hide<>, number | seven)))> {};
  struct Appended
      : rule<many(append<&ast::Values::numbers>(tight(number) | seven))> {};
  struct Node : rule<assign<&ast::Values::node>(
                    tight(node | create<ast::Item>(lit<"!">)))> {};
  struct Nodes
      : rule<many(append<&ast::Values::nodes>(
            skip(ignore<WS>, hide<>, node | create<ast::Item>(lit<"!">))))> {};
  struct Text : rule<number> {};
  struct Measure : rule<as<length>(number)> {};
  struct Root
      : rule<(lit<"direct:"> + ref<Direct>) | (lit<"tight:"> + ref<Tight>) |
             (lit<"scope:"> + ref<Scoped>) | (lit<"choice:"> + ref<Chosen>) |
             (lit<"nested:"> + ref<Nested>) | (lit<"append:"> + ref<Appended>) |
             (lit<"node:"> + ref<Node>) | (lit<"nodes:"> + ref<Nodes>) |
             (lit<"text:"> + assign<&ast::Values::text>(ref<Text>)) |
             (lit<"measure:"> + assign<&ast::Values::number>(ref<Measure>))> {};
  using entry = Root;
  using ignored = WS;
};

struct Suite : Last {
  struct Root : rule<ref<Last::Root> | (lit<"reach:"> + ref<Reach::Root>) |
                     (lit<"inlast:"> + ref<Host<Last>::Root>) |
                     (lit<"inreach:"> + ref<Host<Reach>::Root>) |
                     (lit<"inquoted:"> + ref<Host<Quoted>::Root>) |
                     (lit<"values:"> + ref<SectionValues::Root>)> {};
  using entry = Root;
};
struct Ll : Suite {};
struct Lr : Suite {};

}  // namespace tsecpred
