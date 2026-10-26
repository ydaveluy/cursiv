#pragma once

// One language, twice: the shapes the two engines have to agree on -- a
// choice, a loop, an option inside a sequence, a precedence table and a rule
// whose body is one reference. The two grammars differ by their name alone,
// which is what lets one tree hold both generated modules.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tpar {

namespace ast {
struct Expr : cursiv::Extends<cursiv::AstNode> {};
struct Num : cursiv::Extends<Expr> {
  double value{};
};
struct Name : cursiv::Extends<Expr> {
  string_view id;
};
struct Call : cursiv::Extends<Expr> {
  string_view name;
  vector<Expr*> args;
};
struct Binary : cursiv::Extends<Expr> {
  Expr* left{};
  string_view op;
  Expr* right{};
};
struct Neg : cursiv::Extends<Expr> {
  Expr* operand{};
};
struct Fact : cursiv::Extends<Expr> {
  Expr* operand{};
};
struct Let : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  Expr* guard{};
  Expr* value{};
  bool exported{};
};
struct Item : cursiv::Extends<cursiv::AstNode> {
  Expr* value{};
  string_view alias;
};
struct Show : cursiv::Extends<cursiv::AstNode> {
  vector<Item*> items;
  bool all{};
};
struct Unit : cursiv::Extends<cursiv::AstNode> {
  vector<Let*> lets;
  vector<Show*> shows;
};
}  // namespace ast

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct COMMENT : terminal<lit<"//"> + many(cr<R"(^\r\n)">)> {};
  // A block comment the end of the text closes too.
  struct BLOCK : terminal<through(lit<"/*">, lit<"*/"> | eof)> {};
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};
  struct NUM : terminal<some(d) + option(lit<"."> + some(d)), double> {};

  struct Expression;
  struct Unary;
  struct Postfix;
  struct Primary;

  struct Expression
      : infix<Unary, &ast::Binary::left, &ast::Binary::op,
              &ast::Binary::right,
              (left(lit<"*"> | lit<"/">) >> left(lit<"+"> | lit<"-">))> {};

  // Two rules that write nothing of their own between the table and the arm
  // that parenthesises: the record the table states its member over is then
  // three rules below it, and the parenthesised arm hands back no record at
  // all.
  struct Unary : rule<create<ast::Neg>(lit<"~"> +
                                       assign<&ast::Neg::operand>(ref<Unary>))
                      | ref<Postfix>> {};

  struct Postfix : rule<create<ast::Fact>(
                            assign<&ast::Fact::operand>(ref<Primary>) +
                            lit<"!">)
                        | ref<Primary>> {};

  struct Primary
      : rule<create<ast::Num>(assign<&ast::Num::value>(ref<NUM>))          //
             | create<ast::Call>(assign<&ast::Call::name>(ref<ID>) +
                                 lit<"("> +
                                 option(some(append<&ast::Call::args>(
                                                 ref<Expression>),
                                             lit<",">)) +
                                 lit<")">)                                 //
             | create<ast::Name>(assign<&ast::Name::id>(ref<ID>))          //
             | (lit<"("> + ref<Expression> + lit<")">)> {};

  struct ItemRule;
  struct ShowRule;

  // A directive over an option writes only where the option was taken.
  struct LetRule : rule<option(flag<&ast::Let::exported>(kw<"export">)) +
                        kw<"let"> + assign<&ast::Let::name>(ref<ID>) +
                        assign<&ast::Let::guard>(option(ref<Primary>)) +
                        lit<"="> + assign<&ast::Let::value>(ref<Expression>) +
                        lit<";">> {};

  // The record this one lays rides onto its member's, since everything after
  // that member may be absent -- and where the member hands back no record,
  // the ride is what has to fail rather than land on the last leaf written.
  struct ItemRule
      : rule<assign<&ast::Item::value>(ref<Expression>) +
             option(kw<"as"> + assign<&ast::Item::alias>(ref<ID>))> {};

  struct ShowRule
      : rule<kw<"show"> + flag<&ast::Show::all>(option(kw<"all">)) +
             some(append<&ast::Show::items>(ref<ItemRule>), lit<",">) +
             lit<";">> {};

  struct UnitRule : rule<many(append<&ast::Unit::lets>(ref<LetRule>) |
                              append<&ast::Unit::shows>(ref<ShowRule>))> {};

  using entry = UnitRule;
  using ignore = WS;
  using hidden = list<COMMENT, BLOCK>;
};

// Declarations that open alike and part on their second word.
struct Decls : cursiv::GrammarSpec {
  using WS = Base::WS;
  using ID = Base::ID;
  struct StructRule
      : rule<create<ast::Let>(option(flag<&ast::Let::exported>(kw<"pub">)) +
                              kw<"struct"> + assign<&ast::Let::name>(ref<ID>) +
                              lit<"{"> + lit<"}">)> {};
  struct ClassRule
      : rule<create<ast::Let>(option(flag<&ast::Let::exported>(kw<"pub">)) +
                              kw<"class"> + assign<&ast::Let::name>(ref<ID>) +
                              option(kw<"extends"> + ref<ID>) + lit<"{"> +
                              lit<"}">)> {};
  struct UnitRule : rule<many(append<&ast::Unit::lets>(ref<StructRule>) |
                              append<&ast::Unit::lets>(ref<ClassRule>))> {};

  using entry = UnitRule;
  using ignore = WS;
};

// The declaration recovery cases use the same identifier and whitespace as
// the expression language. Their own scope keeps comments out of declarations.
struct Suite : Base {
  struct Root : rule<ref<Base::UnitRule> |
                     skip(ignored<WS>, hide<>,
                          lit<"decl:"> + ref<Decls::UnitRule> + eof)> {};
  using entry = Root;
};

// The table engine's reading of it, and the descent's. Nothing distinguishes
// them but the type: two names, one language, two modules.
struct Lr : Suite {};
struct Ll : Suite {};

}  // namespace tpar
