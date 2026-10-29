#pragma once

// One language, twice: the shapes the two engines have to agree on -- a
// choice, a loop, an option inside a sequence, a precedence table and a rule
// whose body is one reference. The two grammars differ by their name alone,
// which is what lets one tree hold both generated modules.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "engines/ll/ParityAst.hpp"

namespace tpar {

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct COMMENT : terminal<lit<"//"> + many(cr<R"(^\r\n)">)> {};
  // A block comment the end of the text closes too.
  struct BLOCK : terminal<through(lit<"/*">, lit<"*/"> | eof)> {};
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};
  struct NUM : terminal<as<double>(some(d) + option(lit<"."> + some(d)))> {};

  struct Expression;
  struct Unary;
  struct Postfix;
  struct Primary;

  struct Expression
      : infix<Unary, ast::Binary,
              levels(left(lit<"*"> | lit<"/">), left(lit<"+"> | lit<"-">))> {};

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
  using ignored = WS;
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
  using ignored = WS;
};

// The declaration recovery cases use the same identifier and whitespace as
// the expression language. Their own scope keeps comments out of declarations.
struct Suite : Base {
  struct Root : rule<ref<Base::UnitRule> |
                     skip(ignore<WS>, hide<>,
                          lit<"decl:"> + ref<Decls::UnitRule> + eof)> {};
  using entry = Root;
};

// The table engine's reading of it, and the descent's. Nothing distinguishes
// them but the type: two names, one language, two modules.
struct Lr : Suite {};
struct Ll : Suite {};

}  // namespace tpar
