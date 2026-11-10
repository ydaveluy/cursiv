#pragma once

// One boolean read three ways: a terminal of constant alternatives, a
// terminal converted whole, and a data rule. A size is a terminal of words in
// nested choices, keywords of either case.

#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"

namespace twords {

namespace ast {
struct Item : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  boolean flag = false;
  string_view size;
  reference<Item> target;
};
struct Items : cursiv::Extends<cursiv::AstNode> {
  vector<Item*> items;
};
}  // namespace ast

struct Suite : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct COMMENT : terminal<lit<"//"> + many(cr<R"(^\r\n)">)> {};
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};
  struct BRANCH : terminal<as<true>(ikw<"TRUE">) | as<false>(ikw<"FALSE">)> {};
  struct WHOLE : terminal<as<bool>(ikw<"TRUE"> | ikw<"FALSE">)> {};
  struct Bool : rule<as<true>(ikw<"TRUE">) | as<false>(ikw<"FALSE">)> {};
  struct SIZE : terminal<(kw<"s"> | kw<"small">) | (ikw<"L"> | ikw<"large">)> {
  };

  struct BranchItem : rule<create<ast::Item>(
                          assign<&ast::Item::name>(ref<ID>) +
                          assign<&ast::Item::flag>(ref<BRANCH>) + lit<";">)> {};
  struct WholeItem : rule<create<ast::Item>(
                         assign<&ast::Item::name>(ref<ID>) +
                         assign<&ast::Item::flag>(ref<WHOLE>) + lit<";">)> {};
  struct RuleItem : rule<create<ast::Item>(assign<&ast::Item::name>(ref<ID>) +
                                           assign<&ast::Item::flag>(ref<Bool>) +
                                           lit<";">)> {};
  struct SizeItem : rule<create<ast::Item>(assign<&ast::Item::name>(ref<ID>) +
                                           assign<&ast::Item::size>(ref<SIZE>) +
                                           lit<";">)> {};
  // A name in several tokens, through rules that write nothing on the way.
  struct Part : rule<ref<ID> | kw<"in">> {};
  struct Path : rule<some(ref<Part>, lit<".">)> {};
  struct PathItem
      : rule<create<ast::Item>(assign<&ast::Item::name>(ref<Path>) +
                               lit<"->"> + assign<&ast::Item::target>(ref<Path>) +
                               lit<";">)> {};
  // A keyword beside a reference, after it in the grammar.
  struct CapItem
      : rule<create<ast::Item>(assign<&ast::Item::name>(ref<ID>) + lit<":"> +
                               (assign<&ast::Item::target>(ref<ID>) |
                                kw<"none">) +
                               lit<";">)> {};
  // A reference first in its rule: the caret enters the rule to write it.
  struct NewItem : rule<create<ast::Item>(assign<&ast::Item::target>(ref<ID>) +
                                          lit<";">)> {};
  struct Root
      : rule<create<ast::Items>(
            (lit<"path:"> + many(append<&ast::Items::items>(ref<PathItem>))) |
            (lit<"cap:"> + many(append<&ast::Items::items>(ref<CapItem>))) |
            (lit<"new:"> + many(append<&ast::Items::items>(ref<NewItem>))) |
            (lit<"branch:"> +
             many(append<&ast::Items::items>(ref<BranchItem>))) |
            (lit<"whole:"> + many(append<&ast::Items::items>(ref<WholeItem>))) |
            (lit<"rule:"> + many(append<&ast::Items::items>(ref<RuleItem>))) |
            (lit<"size:"> + many(append<&ast::Items::items>(ref<SizeItem>))))> {
  };
  using entry = Root;
  using ignored = WS;
  using hidden = COMMENT;
};
struct Ll : Suite {};
struct Lr : Suite {};

}  // namespace twords
