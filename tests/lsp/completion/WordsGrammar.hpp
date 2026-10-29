#pragma once

// One boolean read three ways: a terminal whose alternatives each give a
// constant, a terminal converted whole, and the data rule reading the same
// words. A value follows a name directly, so a caret glued to the name is
// asked about it, and a line comment can end right before one. A size is a
// terminal of words in nested choices, keywords of either case.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace twords {

namespace ast {
struct Item : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  bool flag = false;
  string_view size;
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
  struct Root
      : rule<create<ast::Items>(
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
