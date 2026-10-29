#pragma once

// A repair-search probe: a keyword to misspell, three bracket kinds to leave
// open, a separated list to lengthen, and a late-settling alternative.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace trepair {

namespace ast {
struct Item : cursiv::Extends<cursiv::AstNode> {};
struct Word : cursiv::Extends<Item> {
  std::string_view name;
};
struct Group : cursiv::Extends<Item> {
  cursiv::vector<Item*> items;
};
struct Index : cursiv::Extends<Item> {
  cursiv::vector<Item*> items;
};
struct Decl : cursiv::Extends<cursiv::AstNode> {};
struct Element : cursiv::Extends<Decl> {
  std::string_view name;
  cursiv::vector<Item*> items;
};
struct Section : cursiv::Extends<Decl> {
  std::string_view name;
  cursiv::vector<Item*> items;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  cursiv::vector<Decl*> decls;
};
}  // namespace ast

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};

  struct ItemRule;
  struct GroupRule
      : rule<create<ast::Group>(
            lit<"("> +
            many(append<&ast::Group::items>(ref<ItemRule>), lit<",">) +
            lit<")">)> {};
  struct IndexRule
      : rule<create<ast::Index>(
            lit<"["> +
            many(append<&ast::Index::items>(ref<ItemRule>), lit<",">) +
            lit<"]">)> {};
  struct WordRule : rule<create<ast::Word>(assign<&ast::Word::name>(ref<ID>))> {
  };
  struct ItemRule : rule<ref<WordRule> | ref<GroupRule> | ref<IndexRule>> {};

  // Two arms that part only on their closing keyword: the repetition between
  // them is what the reading must carry before it can settle.
  struct ElementRule
      : rule<create<ast::Element>(
            kw<"element"> + assign<&ast::Element::name>(ref<ID>) + lit<"{"> +
            many(append<&ast::Element::items>(ref<ItemRule>), lit<";">) +
            lit<"}"> + kw<"end">)> {};
  struct SectionRule
      : rule<create<ast::Section>(
            kw<"section"> + assign<&ast::Section::name>(ref<ID>) + lit<"{"> +
            many(append<&ast::Section::items>(ref<ItemRule>), lit<";">) +
            lit<"}"> + kw<"done">)> {};

  struct DeclRule : rule<ref<ElementRule> | ref<SectionRule>> {};
  struct Root
      : rule<create<ast::Doc>(many(append<&ast::Doc::decls>(ref<DeclRule>)))> {
  };

  using entry = Root;
  using ignored = WS;
};

struct G : Base {};
struct Lr : Base {};

}  // namespace trepair
