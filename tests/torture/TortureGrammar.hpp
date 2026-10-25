#pragma once

// A throwaway NTTP grammar built to break error recovery, not to ship: three
// bracket kinds for imbrication, a tight (no-skip) header, a through()
// body that can run to EOF, and keywords of three lengths (2/4/9) for the typo
// repair to misjudge differently.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tort {

namespace ast {

struct ExprNode : cursiv::Extends<cursiv::AstNode> {};
struct NumLit : cursiv::Extends<ExprNode> {
  std::string_view text;
};
struct Bin : cursiv::Extends<ExprNode> {
  ExprNode* left = nullptr;
  std::string_view op;
  ExprNode* right = nullptr;
};

struct Item : cursiv::Extends<cursiv::AstNode> {};
struct NumItem : cursiv::Extends<Item> {
  std::string_view text;
};
struct StrItem : cursiv::Extends<Item> {
  std::string_view text;
};
struct ListItem : cursiv::Extends<Item> {
  cursiv::vector<Item*> items;
};

struct Member : cursiv::Extends<cursiv::AstNode> {};
struct TypeDecl : cursiv::Extends<Member> {
  std::string_view name;
  ExprNode* value = nullptr;
};
struct RawDecl : cursiv::Extends<Member> {
  std::string_view name;
  std::string_view body;
};
struct Section : cursiv::Extends<Member> {
  std::string_view name;
  cursiv::vector<Item*> items;
};

struct Program : cursiv::Extends<cursiv::AstNode> {
  std::string_view name;
  cursiv::vector<Member*> members;
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
  // A token-shape mode: `<<<` opens, `>>>` closes, everything between is
  // opaque to the grammar -- the same `through()` shape `BLOCK` uses for
  // comments, but here the region is a value, not trivia.
  struct RAW : terminal<through(lit<"<<<">, lit<">>>">)> {};

  // Not `infix<>`: the engine refuses to compile any grammar combining an
  // infix precedence table with a tight skip() region, even where the table
  // is unreachable from the region. A hand-rolled right-recursive rule
  // sidesteps that gate and still gives the corpus an operator to corrupt.
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

  // Short keyword (2 bytes): the entry, so a typo here has no bare-word
  // escape hatch -- unlike arithmetics' `def`, nothing else in the grammar
  // accepts a document that does not open with this literal.
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

  // Short keyword (2 bytes).
  struct ProgramRule
      : rule<ikw<"go"> + assign<&ast::Program::name>(ref<ID>) + lit<"{"> +
             many(append<&ast::Program::members>(ref<MemberRule>)) + lit<"}">> {
  };

  using entry = ProgramRule;
  using ignore = WS;
  using hidden = list<BLOCK, LINE>;
};

}  // namespace tort
