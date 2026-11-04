#pragma once

// One language, twice: the shapes the two engines have to agree on -- a
// choice, a loop, an option inside a sequence, a precedence table and a rule
// whose body is one reference. The two grammars differ by their name alone,
// which is what lets one tree hold both generated modules.

#include "cursiv/grammar/GrammarSpec.hpp"
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

// Stretches a repair search has to read past: plain sums, arms that part only
// behind a whole sum, and a list whose every call ends where the last one does.
struct Repairs : cursiv::GrammarSpec {
  using WS = Base::WS;
  using NUM = Base::NUM;
  struct Sum;
  struct Term
      : rule<create<ast::Num>(assign<&ast::Num::value>(ref<NUM>)) |
             create<ast::Neg>(lit<"("> + assign<&ast::Neg::operand>(ref<Sum>) +
                              lit<")">)> {};
  struct Sum : rule<create<ast::Call>(
                   append<&ast::Call::args>(ref<Term>) +
                   many(lit<"+"> + append<&ast::Call::args>(ref<Term>)))> {};
  struct Sums : rule<create<ast::Call>(
                    kw<"sums"> + append<&ast::Call::args>(ref<Sum>) +
                    many(lit<","> + append<&ast::Call::args>(ref<Sum>)) +
                    lit<";">)> {};
  struct Assign : rule<create<ast::Call>(append<&ast::Call::args>(ref<Sum>) +
                                         lit<"="> +
                                         append<&ast::Call::args>(ref<Sum>) +
                                         lit<";">)> {};
  struct Value : rule<create<ast::Call>(append<&ast::Call::args>(ref<Sum>) +
                                        lit<";">)> {};
  struct Either : rule<kw<"either"> + (ref<Assign> | ref<Value>)> {};
  struct Short : rule<create<ast::Num>(assign<&ast::Num::value>(ref<NUM>))> {};
  struct Marked : rule<create<ast::Num>(assign<&ast::Num::value>(ref<NUM>) +
                                        lit<"!">)> {};
  struct Items;
  struct Items : rule<create<ast::Call>(
                     append<&ast::Call::args>(ref<Short> | ref<Marked>) +
                     option(lit<","> + append<&ast::Call::args>(ref<Items>)))> {
  };
  struct Plain : rule<create<ast::Call>(kw<"plain"> +
                                        append<&ast::Call::args>(ref<Items>) +
                                        lit<";">)> {};
  struct Closed : rule<create<ast::Call>(kw<"closed"> +
                                         append<&ast::Call::args>(ref<Items>) +
                                         lit<"!"> + lit<";">)> {};
  struct UnitRule : rule<create<ast::Call>(
                        many(append<&ast::Call::args>(
                            ref<Sums> | ref<Either> | ref<Plain> |
                            ref<Closed>)))> {};
};

// Alternatives alike past their first token: in one rule, through a rule of
// one's own, around a rule both call, as rules a loop chooses between, and over
// a word two terminals read.
struct Forks : cursiv::GrammarSpec {
  using WS = Base::WS;
  using ID = Base::ID;
  struct TAG : terminal<cr<"t"> + cr<"g">> {};
  struct Opening : rule<lit<"x">> {};
  struct Run
      : rule<create<ast::Let>((ref<Opening> + many(lit<"a">) + lit<"b">) |
                              (lit<"x"> + many(lit<"a">) + lit<"c">))> {};
  struct Same : rule<create<ast::Let>(
                    (kw<"fi"> + assign<&ast::Let::name>(ref<ID>) + lit<";">) |
                    (kw<"fi"> + assign<&ast::Let::name>(ref<ID>) + lit<"("> +
                     lit<")">))> {};
  struct Called : rule<kw<"in"> + ref<ID>> {};
  struct Around
      : rule<create<ast::Let>((kw<"up"> + ref<Called> + lit<";">) |
                              (kw<"up"> + ref<Called> + lit<"("> + lit<")">))> {
  };
  struct Field : rule<create<ast::Let>(
                     kw<"ty"> + assign<&ast::Let::name>(ref<ID>) + lit<";">)> {
  };
  struct Method
      : rule<create<ast::Let>(kw<"ty"> + assign<&ast::Let::name>(ref<ID>) +
                              lit<"("> + lit<")">)> {};
  struct Twins
      : rule<create<ast::Let>((kw<"tw"> + ref<ID> + ref<ID> + lit<";">) |
                              (kw<"tw"> + ref<TAG> + ref<TAG> + lit<":">))> {};
  struct UnitRule : rule<many(append<&ast::Unit::lets>(ref<Run>) |
                              append<&ast::Unit::lets>(ref<Same>) |
                              append<&ast::Unit::lets>(ref<Around>) |
                              append<&ast::Unit::lets>(ref<Twins>) |
                              append<&ast::Unit::lets>(ref<Field>) |
                              append<&ast::Unit::lets>(ref<Method>))> {};

  using entry = UnitRule;
  using ignored = WS;
};

// What a token at a caret fills: a record it opens for two members, one behind a
// rule of one word, a member past a rule that may read nothing, and a rule that
// reads itself first.
struct Members : cursiv::GrammarSpec {
  using WS = Base::WS;
  using ID = Base::ID;
  struct Box : rule<create<ast::Lit>(kw<"bx">)> {};
  struct Twice : rule<create<ast::Hold>(
                     kw<"twice"> + ((assign<&ast::Hold::first>(ref<Box>) +
                                     lit<"+">) |
                                    (assign<&ast::Hold::second>(ref<Box>) +
                                     lit<"-">)))> {};
  struct Word : rule<kw<"ck">> {};
  struct Worded : rule<create<ast::Lit>(ref<Word>)> {};
  struct Through : rule<create<ast::Hold>(
                       kw<"through"> + assign<&ast::Hold::first>(ref<Worded>) +
                       lit<";">)> {};
  struct Chain;
  struct Chain : rule<create<ast::Chained>(assign<&ast::Chained::left>(
                                               ref<Chain>) +
                                           lit<"."> +
                                           assign<&ast::Chained::name>(ref<ID>)) |
                      create<ast::Lit>(kw<"lx">)> {};
  struct Lone : rule<create<ast::Hold>(kw<"lone"> +
                                       assign<&ast::Hold::first>(ref<Chain>) +
                                       lit<";">)> {};
  struct Both : rule<create<ast::Hold>(
                    kw<"both"> + ((assign<&ast::Hold::first>(ref<Chain>) +
                                   lit<"+">) |
                                  (assign<&ast::Hold::second>(ref<Chain>) +
                                   lit<"-">)))> {};
  // A terminal that writes a member only where it opens a rule, and stands bare
  // past a keyword; the same through a data rule.
  struct TICK : terminal<lit<"$"> + many(w)> {};
  struct Ticked : rule<create<ast::Hold>(assign<&ast::Hold::text>(ref<TICK>) +
                                         lit<"=">)> {};
  struct Bare : rule<create<ast::Hold>(kw<"bare"> + ref<TICK> + lit<";">)> {};
  struct QUOTE : terminal<lit<"%"> + many(w)> {};
  struct Spelt : rule<ref<QUOTE>> {};
  struct Named : rule<create<ast::Hold>(kw<"named"> +
                                        assign<&ast::Hold::text>(ref<Spelt>) +
                                        lit<";">)> {};
  struct Plain : rule<create<ast::Hold>(kw<"plain"> + ref<QUOTE> + lit<";">)> {};
  // A record past a rule that may read nothing.
  struct Mark : rule<option(kw<"mk">)> {};
  struct Marked : rule<create<ast::Lit>(ref<Mark> + kw<"nk">)> {};
  struct Marks : rule<create<ast::Hold>(kw<"marked"> +
                                        assign<&ast::Hold::first>(ref<Marked>) +
                                        lit<";">)> {};
  // A member past a repetition at the head of its rule, in a list of them.
  struct Turn : rule<many(append<&ast::Hold::marks>(ref<Box>)) +
                     ((assign<&ast::Hold::text>(ref<ID> | kw<"in">) +
                       option(lit<"?">)) |
                      kw<"zz"> |
                      (ref<Mark> + assign<&ast::Hold::text>(kw<"nk">))) +
                     lit<"=">> {};
  struct Turns : rule<create<ast::Hold>(
                     kw<"turns"> + lit<"{"> +
                     option(some(append<&ast::Hold::turns>(ref<Turn>), lit<",">) +
                            option(lit<",">)) +
                     lit<"}">)> {};
  struct Stmt : rule<ref<Twice> | ref<Through> | ref<Lone> | ref<Both> |
                     ref<Ticked> | ref<Bare> | ref<Named> | ref<Plain> |
                     ref<Marks> | ref<Turns>> {};
  struct UnitRule : rule<create<ast::Holds>(
                        many(append<&ast::Holds::holds>(ref<Stmt>)))> {};
};
struct Itself : cursiv::GrammarSpec {
  struct Self;
  struct Self : rule<create<ast::Chained>(assign<&ast::Chained::left>(ref<Self>) +
                                          lit<"@"> +
                                          assign<&ast::Chained::name>(
                                              ref<Base::ID>)) |
                     create<ast::Lit>(kw<"s0">)> {};
};

// The declaration recovery cases use the same identifier and whitespace as
// the expression language. Their own scope keeps comments out of declarations.
struct Suite : Base {
  struct Root : rule<ref<Base::UnitRule> |
                     skip(ignore<WS>, hide<>,
                          (lit<"decl:"> + ref<Decls::UnitRule> + eof) |
                              (lit<"fork:"> + ref<Forks::UnitRule> + eof)) |
                     (lit<"rep:"> + ref<Repairs::UnitRule> + eof) |
                     (lit<"member:"> + ref<Members::UnitRule> + eof) |
                     ref<Itself::Self>> {};
  using entry = Root;
};

// The table engine's reading of it, and the descent's. Nothing distinguishes
// them but the type: two names, one language, two modules.
struct Lr : Suite {};
struct Ll : Suite {};

}  // namespace tpar
