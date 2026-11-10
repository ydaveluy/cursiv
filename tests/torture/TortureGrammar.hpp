#pragma once

// A grammar built to break error recovery: three bracket kinds for nesting, a
// tight (no-skip) header, a through() body that can run to the end, and
// keywords of three lengths (2/4/9) for the typo repair.

#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"

namespace tort {

namespace ast {

struct ExprNode : cursiv::Extends<cursiv::AstNode> {};
struct NumLit : cursiv::Extends<ExprNode> {
  string_view text;
};
struct Bin : cursiv::Extends<ExprNode> {
  ExprNode* left = nullptr;
  string_view op;
  ExprNode* right = nullptr;
};

struct Item : cursiv::Extends<cursiv::AstNode> {};
struct NumItem : cursiv::Extends<Item> {
  string_view text;
};
struct StrItem : cursiv::Extends<Item> {
  string_view text;
};
struct ListItem : cursiv::Extends<Item> {
  vector<Item*> items;
};

struct Member : cursiv::Extends<cursiv::AstNode> {};
struct TypeDecl : cursiv::Extends<Member> {
  string_view name;
  ExprNode* value = nullptr;
};
struct RawDecl : cursiv::Extends<Member> {
  string_view name;
  string_view body;
};
struct Section : cursiv::Extends<Member> {
  string_view name;
  vector<Item*> items;
};

struct Program : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  vector<Member*> members;
};

}  // namespace ast

struct Torture : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct LINE : terminal<lit<"//"> + many(cr<R"(^\r\n)">)> {};
  struct BLOCK : terminal<through(lit<"/*">, lit<"*/">)> {};

  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};
  struct NUM : terminal<some(d)> {};
  struct STR : terminal<lit<"\""> + many((lit<"\\"> + any) | cr<R"(^"\\)">) +
                        lit<"\"">> {};
  // `<<<` opens, `>>>` closes, and everything between is one token: the
  // `through()` shape `BLOCK` uses for comments, but as a value.
  struct RAW : terminal<through(lit<"<<<">, lit<">>>">)> {};

  // A right-recursive rule gives the corpus an operator to corrupt.
  struct ExprRule;
  struct PrimaryRule
      : rule<create<ast::NumLit>(assign<&ast::NumLit::text>(ref<NUM>))  //
             | (lit<"("> + ref<ExprRule> + lit<")">)> {};
  struct ExprRule : rule<(ref<PrimaryRule> +
                          nest<ast::Bin, &ast::Bin::left>(
                              assign<&ast::Bin::op>(lit<"+"> | lit<"-"> |
                                                    lit<"*"> | lit<"/">) +
                              assign<&ast::Bin::right>(ref<ExprRule>)))  //
                         | ref<PrimaryRule>> {};

  struct ItemRule
      : rule<create<ast::NumItem>(assign<&ast::NumItem::text>(ref<NUM>))    //
             | create<ast::StrItem>(assign<&ast::StrItem::text>(ref<STR>))  //
             |
             create<ast::ListItem>(
                 lit<"("> +
                 many(append<&ast::ListItem::items>(ref<ItemRule>), lit<",">) +
                 lit<")">)> {};

  // Medium keyword (4 bytes).
  struct TypeDeclRule
      : rule<create<ast::TypeDecl>(
            ikw<"kind"> + assign<&ast::TypeDecl::name>(ref<ID>) + lit<"="> +
            assign<&ast::TypeDecl::value>(ref<ExprRule>) + lit<";">)> {};

  // Long keyword (9 bytes), guarding a through() value: an unclosed `<<<`
  // here is a mode that never returns.
  struct RawDeclRule
      : rule<create<ast::RawDecl>(ikw<"blueprint"> +
                                  assign<&ast::RawDecl::name>(ref<ID>) +
                                  assign<&ast::RawDecl::body>(ref<RAW>))> {};

  // Tight header: inside `[` name `]` the skipper is gone -- a space
  // anywhere in it is a byte the region does not ignore.
  static constexpr auto head =
      tight(lit<"["> + assign<&ast::Section::name>(ref<ID>) + lit<"]">);
  struct SectionRule
      : rule<create<ast::Section>(
            head + lit<"{"> +
            many(append<&ast::Section::items>(ref<ItemRule>), lit<",">) +
            lit<"}">)> {};

  struct MemberRule
      : rule<ref<TypeDeclRule> | ref<RawDeclRule> | ref<SectionRule>> {};

  // Short keyword (2 bytes), the entry: nothing else accepts a document that
  // does not open with it, so a typo here has no fallback.
  struct ProgramRule
      : rule<ikw<"go"> + assign<&ast::Program::name>(ref<ID>) + lit<"{"> +
             many(append<&ast::Program::members>(ref<MemberRule>)) + lit<"}">> {
  };

  using entry = ProgramRule;
  using ignored = WS;
  using hidden = list<BLOCK, LINE>;
};

}  // namespace tort
