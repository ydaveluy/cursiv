#pragma once

// The shape a caret is read against: a keyword the author gave a name of its
// own, a k=2 cell with two arms, a construct a repair can leave open, one
// member written from two sites -- one narrowed, one not -- and one written
// by a rule that lays a record of its own.

#include <string_view>

#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"

namespace tcaret {

namespace ast {
struct Stmt : cursiv::Extends<cursiv::AstNode> {};
struct Decl : cursiv::Extends<Stmt> {};
struct Use : cursiv::Extends<Stmt> {};
struct Word : cursiv::Extends<Stmt> {};
struct KwWord : cursiv::Extends<Word> {};
struct IKwWord : cursiv::Extends<Word> {};
struct Param : cursiv::Extends<cursiv::AstNode> {
  std::string_view name;
};
struct Fn : cursiv::Extends<cursiv::AstNode> {
  std::string_view name;
  cursiv::vector<Param*> params;
  cursiv::AstNode* body = nullptr;
};
struct Call : cursiv::Extends<Stmt> {
  std::string_view name;
  cursiv::vector<Stmt*> args;
};
struct Sym : cursiv::Extends<Stmt> {
  std::string_view name;
};
struct Alpha : cursiv::Extends<Sym> {};
struct Beta : cursiv::Extends<Sym> {};
struct Link : cursiv::Extends<Stmt> {
  cursiv::reference<Sym> to;
};
// A SECOND CLASS SPELLING `to`: the member name alone no longer says which
// reference a site writes, so only the class the grammar names can.
struct Bond : cursiv::Extends<cursiv::AstNode> {
  cursiv::reference<Beta> to;
  std::string_view note;
};
struct Tie : cursiv::Extends<Stmt> {
  Bond* bond = nullptr;
};
struct Nest : cursiv::Extends<Stmt> {
  cursiv::reference<Alpha> to;
  Stmt* inner = nullptr;
};
// Each writes its own member: where a token may open either, both are read.
struct SeesAlpha : cursiv::Extends<Stmt> {
  std::string_view name;
  cursiv::reference<Alpha> alpha;
  std::string_view text;
};
struct SeesBeta : cursiv::Extends<Stmt> {
  std::string_view name;
  cursiv::reference<Beta> beta;
};
struct Instance : cursiv::Extends<Stmt> {
  Stmt* instance = nullptr;
};
struct Held : cursiv::Extends<cursiv::AstNode> {};
struct Box : cursiv::Extends<Held> {};
struct Bag : cursiv::Extends<Held> {};
struct Hold : cursiv::Extends<Stmt> {
  Held* inner = nullptr;
};
struct Block : cursiv::Extends<cursiv::AstNode> {
  cursiv::vector<Stmt*> stmts;
};
struct File : cursiv::Extends<cursiv::AstNode> {
  cursiv::vector<Fn*> fns;
};
}  // namespace ast

// ID is declared first on purpose: a literal beats a named terminal at equal
// match length whatever the order, and only that order proves it.
struct Caret : cursiv::GrammarSpec {
  // Names reach past ASCII, and the word boundary with them.
  static constexpr auto word_boundary = cr<R"(a-zA-Z0-9_\u{80}-\u{10FFFF})">;
  struct WS : terminal<some(s)> {};
  struct ID
      : terminal<cr<R"(a-zA-Z_\u{80}-\u{10FFFF})"> + many(word_boundary)> {};
  struct KW_END : terminal<kw<"end">> {};
  struct IKW_LET : terminal<ikw<"let">> {};
  struct AT : terminal<lit<"@">> {};
  struct TILDE : terminal<lit<"~">> {};
  struct BANG : terminal<lit<"!">> {};
  struct QMARK : terminal<lit<"?">> {};

  // Two unit-rename chains over the same shift: the state after `@` reduces
  // either, and the readings only part one token past `~`.
  struct FooB : rule<ref<AT>> {};
  struct FooA : rule<ref<FooB>> {};
  struct Foo : rule<ref<FooA>> {};
  struct BarB : rule<ref<AT>> {};
  struct BarA : rule<ref<BarB>> {};
  struct Bar : rule<ref<BarA>> {};

  struct DeclR
      : rule<create<ast::Decl>(ref<Foo> + ref<TILDE> + ref<BANG> + lit<";">)> {
  };
  struct UseR
      : rule<create<ast::Use>(ref<Bar> + ref<TILDE> + ref<QMARK> + lit<";">)> {
  };

  struct StmtR;
  struct NestR : rule<create<ast::Nest>(
                     kw<"nest"> + assign<&ast::Nest::to>(ref<ID>) + lit<"{"> +
                     assign<&ast::Nest::inner>(ref<StmtR>) + lit<"}">)> {};
  struct WordR : rule<create<ast::KwWord>(ref<KW_END> + lit<";">) |
                      create<ast::IKwWord>(ref<IKW_LET> + lit<";">)> {};
  struct CallR : rule<create<ast::Call>(
                     assign<&ast::Call::name>(ref<ID>) + lit<"("> +
                     many(append<&ast::Call::args>(ref<StmtR>)) + lit<")">)> {};
  struct AlphaR
      : rule<create<ast::Alpha>(
            kw<"alpha"> + assign<&ast::Alpha::name>(ref<ID>) + lit<";">)> {};
  struct BetaR
      : rule<create<ast::Beta>(kw<"beta"> + assign<&ast::Beta::name>(ref<ID>) +
                               lit<";">)> {};
  // Two sites of `Link::to`: `see` binds an `Alpha` alone, `any` whatever the
  // member accepts.
  struct SeeR : rule<create<ast::Link>(
                    kw<"see"> +
                    assign<cursiv::xref<&ast::Link::to, ast::Alpha>>(ref<ID>) +
                    lit<";">)> {};
  // The reference stands last but for an optional, so one branch of the rule
  // is a single symbol and lays the record's own chain on the reference's own
  // slot: two chains for one write, which the caret must still read as one.
  struct TieBody
      : rule<assign<cursiv::xref<&ast::Bond::to, ast::Beta>>(ref<ID>) +
             option(assign<&ast::Bond::note>(ref<ID>))> {};
  struct TieR
      : rule<create<ast::Tie>(
            kw<"tie"> + assign<&ast::Tie::bond>(ref<TieBody>) + lit<";">)> {};
  struct AnyR : rule<create<ast::Link>(
                    kw<"any"> + assign<&ast::Link::to>(ref<ID>) + lit<";">)> {};
  // A member written by a whole rule that lays a record of its own: the two
  // cannot share a chain, so the write stays on the production and the caret
  // is the only reader left to find it there.
  struct BoxR : rule<create<ast::Box>(kw<"box"> + lit<";">)> {};
  struct BagR : rule<create<ast::Bag>(kw<"bag"> + lit<";">)> {};
  struct HoldR : rule<create<ast::Hold>(
                     assign<&ast::Hold::inner>(ref<BoxR> | ref<BagR>))> {};
  // One token, two members: read directly, through a data rule, through rules
  // that open on it, and beside a reading that fills nothing.
  struct EitherR
      : rule<create<ast::SeesAlpha>(kw<"either"> +
                                    assign<&ast::SeesAlpha::alpha>(ref<ID>) +
                                    lit<"+">) |
             create<ast::SeesBeta>(kw<"either"> +
                                   assign<&ast::SeesBeta::beta>(ref<ID>) +
                                   lit<"-">)> {};
  struct Dotted : rule<ref<ID> + many(lit<"."> + ref<ID>)> {};
  struct DottedR
      : rule<create<ast::SeesAlpha>(kw<"dotted"> +
                                    assign<&ast::SeesAlpha::alpha>(ref<Dotted>) +
                                    lit<"+">) |
             create<ast::SeesBeta>(kw<"dotted"> +
                                   assign<&ast::SeesBeta::beta>(ref<Dotted>) +
                                   lit<"-">)> {};
  struct OpensAlpha : rule<create<ast::SeesAlpha>(
                          assign<&ast::SeesAlpha::alpha>(ref<ID>) + lit<"+">)> {};
  struct OpensBeta : rule<create<ast::SeesBeta>(
                         assign<&ast::SeesBeta::beta>(ref<ID>) + lit<"-">)> {};
  struct OpensR : rule<kw<"opens"> + (ref<OpensAlpha> | ref<OpensBeta>)> {};
  struct MaybeR
      : rule<create<ast::SeesAlpha>(kw<"maybe"> +
                                    assign<&ast::SeesAlpha::alpha>(ref<ID>) +
                                    lit<"+">) |
             create<ast::SeesBeta>(kw<"maybe"> + ref<ID> + lit<"-">)> {};
  // Two rules sharing a prefix, each going on to a member behind data rules: one
  // through a list of the other's, and one of them with a string beside it.
  struct Word : rule<ref<ID> | kw<"word">> {};
  struct Words : rule<some(ref<Word>, lit<".">)> {};
  struct STR : terminal<lit<"'"> + many(cr<"a-z">) + lit<"'">> {};
  struct ModelR
      : rule<create<ast::SeesAlpha>(
            assign<&ast::SeesAlpha::name>(ref<Word>) +
            option(lit<":"> + (assign<&ast::SeesAlpha::alpha>(ref<Words>) |
                               assign<&ast::SeesAlpha::text>(ref<STR>))) +
            option(lit<"{"> + lit<"}">))> {};
  struct AssemblyR
      : rule<create<ast::SeesBeta>(assign<&ast::SeesBeta::name>(ref<Word>) +
                                   lit<":"> +
                                   assign<&ast::SeesBeta::beta>(ref<Word>) +
                                   lit<"<"> + lit<">">)> {};
  struct InstanceR : rule<create<ast::Instance>(
                         kw<"child"> + assign<&ast::Instance::instance>(
                                           ref<ModelR> | ref<AssemblyR>))> {};
  struct StmtR : rule<ref<DeclR> | ref<UseR> | ref<WordR> | ref<CallR> |
                      ref<AlphaR> | ref<BetaR> | ref<SeeR> | ref<TieR> |
                      ref<AnyR> | ref<HoldR> | ref<NestR> | ref<EitherR> |
                      ref<DottedR> | ref<OpensR> | ref<MaybeR> | ref<InstanceR>> {};
  struct BlockR : rule<create<ast::Block>(
                      lit<"{"> + many(append<&ast::Block::stmts>(ref<StmtR>)) +
                      lit<"}">)> {};
  struct ParamR : rule<create<ast::Param>(assign<&ast::Param::name>(ref<ID>))> {
  };
  struct FnR : rule<create<ast::Fn>(
                   kw<"fn"> + assign<&ast::Fn::name>(ref<ID>) + lit<"("> +
                   many(append<&ast::Fn::params>(ref<ParamR>)) + lit<")"> +
                   assign<&ast::Fn::body>(ref<BlockR>))> {};
  struct FileR
      : rule<create<ast::File>(many(append<&ast::File::fns>(ref<FnR>)))> {};
  using entry = FileR;
  using ignored = WS;
};

}  // namespace tcaret
