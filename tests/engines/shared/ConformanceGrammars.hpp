#pragma once

// The conformance cases of the grammar reference, one grammar each. Letters
// are distinct tokens, blanks are skipped and the whole input is read.

#include "cursiv/grammar/Conversion.hpp"
#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "ConditionContractGrammar.hpp"
namespace tconformance {

namespace ast {
struct Doc : cursiv::Extends<cursiv::AstNode> {};
struct Arms : cursiv::Extends<cursiv::AstNode> {
  string_view low;
  string_view name;
};
struct Pick : cursiv::Extends<cursiv::AstNode> {
  string_view literal;
  string_view keyword;
};
struct First : cursiv::Extends<cursiv::AstNode> {};
struct Second : cursiv::Extends<cursiv::AstNode> {};
struct Won : cursiv::Extends<cursiv::AstNode> {
  string_view token;
  string_view name;
};
struct Wons : cursiv::Extends<cursiv::AstNode> {
  vector<Won*> all;
};
struct Tally : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> turns;
  boolean tail = false;
};
struct Kept : cursiv::Extends<cursiv::AstNode> {
  string_view value = "kept";
};
struct List : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> items;
};
struct A : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> letters;
};
struct B : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> letters;
};
struct C : cursiv::Extends<cursiv::AstNode> {
  string_view letter;
};
struct S : cursiv::Extends<cursiv::AstNode> {
  A* a = nullptr;
  B* b = nullptr;
  C* c = nullptr;
};
struct Leaf : cursiv::Extends<cursiv::AstNode> {
  string_view text;
};
struct Stated : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  vector<Stated*> body;
};
struct Expression : cursiv::Extends<cursiv::AstNode> {};
struct Name : cursiv::Extends<Expression> {
  string_view name;
};
struct Call : cursiv::Extends<Expression> {
  Expression* callee = nullptr;
  Expression* argument = nullptr;
};
struct Program : cursiv::Extends<cursiv::AstNode> {
  vector<Expression*> statements;
};
struct Turn : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> letters;
};
struct Turns : cursiv::Extends<cursiv::AstNode> {
  vector<Turn*> turns;
};
struct Clause : cursiv::Extends<cursiv::AstNode> {
  string_view conjunction;
  string_view modifier;
  string_view field;
  string_view op;
  string_view value;
};
struct Query : cursiv::Extends<cursiv::AstNode> {
  vector<Clause*> clauses;
};
struct Level : cursiv::Extends<cursiv::AstNode> {
  First* first = nullptr;
  Second* second = nullptr;
  Level* next = nullptr;
};
}  // namespace ast

struct Keyword : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root : rule<create<ast::Doc>(kw<"if">)> {};
  using entry = Root;
  using ignored = WS;
};

struct Arms : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct LOW : terminal<some(cr<"a-z">)> {};
  struct NAME : terminal<some(cr<"a-zA-Z">)> {};
  struct Root : rule<create<ast::Arms>(
                    (assign<&ast::Arms::low>(ref<LOW>) + lit<";">) |
                    (assign<&ast::Arms::name>(ref<NAME>) + lit<":">))> {};
  using entry = Root;
  using ignored = WS;
};

struct CaretGuards : cursiv::GrammarSpec {
  struct Plain : terminal<lit<"x"> + !lit<"!">> {};
  struct BeforeBang : terminal<lit<"x"> + &lit<"!">> {};
  struct Label : terminal<some(cr<"a-z">) + &lit<":">> {};
  struct Number : terminal<some(d)> {};
  struct Stem : terminal<lit<"p"> | lit<"p!a">> {};
  struct Tail : terminal<lit<"!"> + cr<"ab"> + lit<"+">> {};
  struct Root : rule<create<ast::Won>(
      (ref<Stem> + option(assign<&ast::Won::name>(ref<Tail>))) |
      (ref<Plain> + (lit<";"> | lit<"!">)) | (ref<BeforeBang> + lit<"!!">) |
      (lit<"@"> + ((assign<&ast::Won::name>(ref<Label>) + lit<":">) |
                   (assign<&ast::Won::token>(ref<Number>) + lit<";">))))> {};
};

// The tie of `Arms` where one terminal is read behind a condition the lexer
// checks apart from its other terminals: a field is never a number. A word
// stands where no field prefix can complete.
struct Fields : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  static constexpr auto Char = w | lit<"*">;
  struct FIELD : terminal<!(some(d) + !Char) + some(Char)> {};
  struct WORD : terminal<some(Char)> {};
  struct Clause
      : rule<create<ast::Clause>(
            option(assign<&ast::Clause::conjunction>(kw<"AND"> | kw<"OR">)) +
            option(assign<&ast::Clause::modifier>(lit<"+"> | lit<"-">)) +
            ((assign<&ast::Clause::field>(ref<FIELD>) +
              assign<&ast::Clause::op>(lit<"<">) +
              assign<&ast::Clause::value>(ref<WORD>)) |
             (option(assign<&ast::Clause::field>(ref<FIELD>) +
                     assign<&ast::Clause::op>(lit<":">)) +
              assign<&ast::Clause::value>(ref<WORD>))))> {};
  struct Root : rule<create<ast::Query>(
                    many(append<&ast::Query::clauses>(ref<Clause>)))> {};
  using entry = Root;
  using ignored = WS;
};

struct KeywordTie : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct LIT_IF : terminal<lit<"if">> {};
  struct KW_IF : terminal<kw<"if"> | kw<"while">> {};
  struct Root : rule<create<ast::Pick>(
                    (assign<&ast::Pick::literal>(ref<LIT_IF>) + lit<"(">) |
                    (assign<&ast::Pick::keyword>(ref<KW_IF>) + lit<"(">))> {};
  using entry = Root;
  using ignored = WS;
};

struct KeywordTieBoundary : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct LIT_IF : terminal<lit<"if">> {};
  struct KW_IF : terminal<kw<"if"> | kw<"while">> {};
  struct Root : rule<create<ast::Pick>(
                    (assign<&ast::Pick::literal>(ref<LIT_IF>) + lit<"x">) |
                    (assign<&ast::Pick::keyword>(ref<KW_IF>) + lit<"x">))> {};
  using entry = Root;
  using ignored = WS;
};

struct FirstArm : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root : rule<create<ast::First>(lit<"if"> + kw<"then">) |
                    create<ast::Second>(kw<"if"> + lit<"then">)> {};
  using entry = Root;
  using ignored = WS;
};

// The cases the election leaves to the grammar's order, each behind a digit:
// keywords against names, a repetition before an option, and a conversion
// that refuses the elected reading.
inline constexpr auto refuseText = [](std::string_view) {
  return cursiv::ConversionResult<std::string_view>::err("refused");
};
inline constexpr auto keepText = [](std::string_view text) {
  return cursiv::ConversionResult<std::string_view>::ok(text);
};

struct Elected : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NAME : terminal<some(cr<"a-zA-Z">)> {};
  struct COND : terminal<kw<"if"> + &lit<"(">> {};
  struct BadLit : terminal<as<refuseText>(lit<"if">)> {};
  struct GoodKw : terminal<as<keepText>(kw<"if">)> {};
  struct Root
      : rule<(lit<"1"> + (create<ast::First>(kw<"if"> + lit<";">) |
                          create<ast::Second>(lit<"if"> + lit<":">))) |
             (lit<"2"> + create<ast::Won>(assign<&ast::Won::name>(ref<NAME>) |
                                          assign<&ast::Won::token>(kw<"if">))) |
             (lit<"3"> +
              create<ast::Won>(
                  (assign<&ast::Won::name>(ref<NAME>) + lit<"(">) |
                  (assign<&ast::Won::token>(ref<COND>) + lit<"(">))) |
             (lit<"4"> +
              create<ast::Tally>(many(append<&ast::Tally::turns>(lit<"if">)) +
                                 option(flag<&ast::Tally::tail>(kw<"if">)))) |
             (lit<"5"> + create<ast::Kept>(assign<&ast::Kept::value>(
                             ref<BadLit> | ref<GoodKw>)))> {};
  using entry = Root;
  using ignored = WS;
};

struct LiteralsRank : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NAME : terminal<some(cr<"a-zA-Z">)> {};
  struct LITS : terminal<lit<"if"> | lit<"while">> {};
  struct Root : rule<create<ast::Won>(assign<&ast::Won::name>(ref<NAME>) |
                                      assign<&ast::Won::token>(ref<LITS>))> {
  };
  using entry = Root;
  using ignored = WS;
};

struct LiteralsRankRefuses : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NAME : terminal<some(cr<"a-zA-Z">)> {};
  struct LITS : terminal<lit<"if"> | lit<"while">> {};
  struct Root : rule<create<ast::Won>(
                    (assign<&ast::Won::token>(ref<LITS>) + lit<";">) |
                    (assign<&ast::Won::name>(ref<NAME>) + &eof))> {};
  using entry = Root;
  using ignored = WS;
};

struct MixedRank : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NAME : terminal<some(cr<"a-zA-Z">)> {};
  struct LIT_IF : terminal<lit<"if">> {};
  struct MIX : terminal<ref<NAME> | kw<"if">> {};
  struct OPEN : terminal<lit<"{"> | cr<"A-Z">> {};
  struct Root : rule<create<ast::Pick>(
                    (assign<&ast::Pick::literal>(ref<LIT_IF>) + lit<"(">) |
                    (assign<&ast::Pick::keyword>(ref<MIX>) + lit<"(">) |
                    (lit<":"> + ref<OPEN>))> {};
  using entry = Root;
  using ignored = WS;
};

// The literal keeps its rank through a test before a call of its terminal,
// a test after it, and a test that waits for what follows.
struct MixedRefused : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NAME : terminal<some(cr<"a-zA-Z">)> {};
  struct MIX : terminal<(lit<"if"> | ref<NAME>) + !cr<"0-9">> {};
  struct LEADING : terminal<&lit<"i"> + ref<MIX>> {};
  struct TRAILING : terminal<ref<MIX> + &lit<";">> {};
  struct WAITING : terminal<(lit<"if"> | ref<NAME>) + &lit<";">> {};
  template <class Token>
  static constexpr auto pick =
      (assign<&ast::Won::name>(ref<NAME>) | assign<&ast::Won::token>(ref<Token>)) + lit<";">;
  struct Root : rule<create<ast::Won>(pick<MIX> | (lit<"<"> + pick<LEADING>) |
                                      (lit<">"> + pick<TRAILING>) |
                                      (lit<"="> + pick<WAITING>))> {};
  using entry = Root;
  using ignored = WS;
};

struct ConditionedRank : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct LIT_IF : terminal<lit<"if">> {};
  struct SEMI : terminal<lit<"if"> | (kw<"if"> + &lit<";">)> {};
  struct Root : rule<create<ast::Pick>(
                    (assign<&ast::Pick::literal>(ref<LIT_IF>) +
                     (lit<";"> | lit<":">)) |
                    (assign<&ast::Pick::keyword>(ref<SEMI>) +
                     (lit<";"> | lit<":">)))> {};
  using entry = Root;
  using ignored = WS;
};

struct ConditionedRankSwapped : ConditionedRank {
  struct Root : rule<create<ast::Pick>(
                    (assign<&ast::Pick::keyword>(ref<SEMI>) +
                     (lit<";"> | lit<":">)) |
                    (assign<&ast::Pick::literal>(ref<LIT_IF>) +
                     (lit<";"> | lit<":">)))> {};
  using entry = Root;
};

struct LiteralConditionRank : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NAME : terminal<some(cr<"a-zA-Z">)> {};
  struct COND : terminal<lit<"if"> + &lit<";">> {};
  struct Root : rule<create<ast::Won>(
                    (assign<&ast::Won::name>(ref<NAME>) + lit<";">) |
                    (assign<&ast::Won::token>(ref<COND>) + lit<";">))> {};
  using entry = Root;
  using ignored = WS;
};

struct PatternsTie : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NAME : terminal<some(cr<"a-zA-Z">)> {};
  struct TWO : terminal<lit<"i"> + lit<"f">> {};
  struct Root : rule<create<ast::Won>(assign<&ast::Won::token>(ref<TWO>) |
                                      assign<&ast::Won::name>(ref<NAME>))> {};
  using entry = Root;
  using ignored = WS;
};

struct PatternsTieSwapped : PatternsTie {
  struct Root : rule<create<ast::Won>(assign<&ast::Won::name>(ref<NAME>) |
                                      assign<&ast::Won::token>(ref<TWO>))> {
  };
  using entry = Root;
};

struct KeywordTrivia : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NOTE : terminal<kw<"if">> {};
  struct NAME : terminal<some(cr<"a-zA-Z">)> {};
  struct Root : rule<create<ast::Won>(assign<&ast::Won::name>(ref<NAME>))> {};
  using entry = Root;
  using ignored = WS;
  using hidden = NOTE;
};

struct LiteralTrivia : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NOTE : terminal<lit<"if">> {};
  struct NAME : terminal<some(cr<"a-zA-Z">)> {};
  struct Root : rule<create<ast::Won>(assign<&ast::Won::name>(ref<NAME>))> {};
  using entry = Root;
  using ignored = WS;
  using hidden = NOTE;
};

struct ConditionedTrivia : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NOTE : terminal<kw<"if"> + &lit<";">> {};
  struct NAME : terminal<some(cr<"a-zA-Z">)> {};
  struct Root : rule<create<ast::Won>(assign<&ast::Won::name>(ref<NAME>) +
                                      lit<";">)> {};
  using entry = Root;
  using ignored = WS;
  using hidden = NOTE;
};

struct GuardedLiteral : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NOTE : terminal<kw<"if">> {};
  struct NAME : terminal<some(cr<"a-zA-Z">)> {};
  struct LIT : terminal<lit<"if", !lit<"(">>> {};
  struct Root : rule<create<ast::Won>(
                    (assign<&ast::Won::token>(ref<LIT>) + lit<";">) |
                    (assign<&ast::Won::name>(ref<NAME>) + lit<"(">))> {};
  using entry = Root;
  using ignored = WS;
  using hidden = NOTE;
};

// The `/` read where the end of input fails the guard of the other keeps its
// priority over trivia that begins with it.
struct FallbackKeyword : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct COMMENT : terminal<through(lit<"/*">, lit<"*/">)> {};
  struct Root
      : rule<create<ast::Doc>(lit<"x"> + (lit<"/", eof> | (kw<"/"> + lit<"**/">)))> {};
  using entry = Root;
};

struct LiteralBesideGuard : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NAME : terminal<some(cr<"a-zA-Z">)> {};
  struct MIX : terminal<lit<"if"> | some(!lit<"//"> + cr<"a-z">)> {};
  struct Root : rule<create<ast::Won>(assign<&ast::Won::name>(ref<NAME>) |
                                      assign<&ast::Won::token>(ref<MIX>))> {};
  using entry = Root;
  using ignored = WS;
};

struct MergedLookahead : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct TEXT : terminal<some(cr<"a-z.">)> {};
  struct Path : rule<ref<ID> + many(lit<"."> + ref<ID>)> {};
  struct Val;
  struct Array : rule<lit<"["> + many(ref<Val>) + lit<"]">> {};
  struct Val : rule<ref<Array> | (lit<"@"> + ref<Path>) | ref<TEXT>> {};
  struct Key : rule<ref<Path> + option(lit<":"> + ref<Val>)> {};
  struct Root : rule<create<ast::Doc>(many(ref<Key>))> {};
  using entry = Root;
  using ignored = WS;
};

struct MixedLeading : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NAME : terminal<some(cr<"a-zA-Z">)> {};
  struct MIX : terminal<!lit<"_"> + (lit<"if"> | ref<NAME>)> {};
  struct Root : rule<create<ast::Won>(
                    (assign<&ast::Won::name>(ref<NAME>) + lit<";">) |
                    (assign<&ast::Won::token>(ref<MIX>) + lit<";">))> {};
  using entry = Root;
  using ignored = WS;
};

struct ExpectedKeyword : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct REMARK
      : terminal<ikw<"REM"> + some(cr<R"( \t)">) + many(cr<R"(^\r\n)">)> {};
  struct NAME : terminal<!ikw<"REM"> + some(cr<"a-zA-Z">)> {};
  struct Root : rule<create<ast::Won>(assign<&ast::Won::token>(ikw<"REM">) +
                                      lit<";"> +
                                      assign<&ast::Won::name>(ref<NAME>))> {};
  using entry = Root;
  using ignored = WS;
  using hidden = REMARK;
};

// The same beyond ASCII: `ÿ` and `Ÿ` start on different bytes, so the
// keyword reads a choice of cases, and is still the keyword where expected.
// `NAME` refuses it in any case.
struct FoldedKeyword : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct REMARK
      : terminal<ikw<"haÿ"> + some(cr<R"( \t)">) + many(cr<R"(^\r\n)">)> {};
  struct NAME : terminal<!ikw<"haÿ"> + cr<"a-zA-Z"> + many(cr<"a-zA-ZÿŸ">)> {};
  struct Root
      : rule<create<ast::Won>(assign<&ast::Won::token>(ikw<"haÿ">) + lit<";"> +
                              assign<&ast::Won::name>(ref<NAME>))> {};
  using entry = Root;
  using ignored = WS;
  using hidden = REMARK;
};

struct Separated : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root : rule<create<ast::List>(
                    many(append<&ast::List::items>(lit<"a">), lit<",">))> {};
  using entry = Root;
  using ignored = WS;
};

struct Trailing : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root : rule<create<ast::List>(
                    option(some(append<&ast::List::items>(lit<"a">), lit<",">) +
                           option(lit<",">)))> {};
  using entry = Root;
  using ignored = WS;
};

struct Factoring : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct A : rule<create<ast::A>((append<&ast::A::letters>(lit<"x">) +
                                  append<&ast::A::letters>(lit<"y">)) |
                                 append<&ast::A::letters>(lit<"x">))> {};
  struct B : rule<create<ast::B>(append<&ast::B::letters>(lit<"y">) +
                                 append<&ast::B::letters>(lit<"z">))> {};
  struct C : rule<create<ast::C>(assign<&ast::C::letter>(lit<"z">))> {};
  using ignored = WS;
};

struct Grouped : Factoring {
  struct Root : rule<create<ast::S>(assign<&ast::S::a>(ref<A>) +
                                    (assign<&ast::S::b>(ref<B>) |
                                     assign<&ast::S::c>(ref<C>)))> {};
  using entry = Root;
};

struct Repeated : Factoring {
  struct Root
      : rule<create<ast::S>(
            (assign<&ast::S::a>(ref<A>) + assign<&ast::S::b>(ref<B>)) |
            (assign<&ast::S::a>(ref<A>) + assign<&ast::S::c>(ref<C>)))> {};
  using entry = Root;
};

struct Turn : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Item
      : rule<create<ast::Turn>(
            (append<&ast::Turn::letters>(lit<"a">) +
             append<&ast::Turn::letters>(lit<"a">)) |
            append<&ast::Turn::letters>(lit<"a">))> {};
  struct Root : rule<create<ast::Turns>(
                    many(append<&ast::Turns::turns>(ref<Item>)))> {};
  using entry = Root;
  using ignored = WS;
};

struct TurnShortFirst : Turn {
  struct Item
      : rule<create<ast::Turn>(
            append<&ast::Turn::letters>(lit<"a">) |
            (append<&ast::Turn::letters>(lit<"a">) +
             append<&ast::Turn::letters>(lit<"a">)))> {};
  struct Root : rule<create<ast::Turns>(
                    many(append<&ast::Turns::turns>(ref<Item>)))> {};
  using entry = Root;
};

struct Tight : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Root : rule<create<ast::Doc>(tight(lit<"["> + ref<ID> + lit<"]">))> {};
  using entry = Root;
  using ignored = WS;
};

struct Adjacent : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root : rule<create<ast::Doc>(lit<">"> + gap::none + lit<">">)> {};
  using entry = Root;
  using ignored = WS;
};

// A call is attached to its name, and `ARGS` spells `()` too; both names read
// one word. Where a blank parts the name from `()`, the gap refuses the call,
// and its `()` takes no part in the reading of the arguments.
struct AttachedCall : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct NAME : terminal<some(cr<"a-z0-9">)> {};
  struct ARGS : terminal<lit<"("> + many(cr<"^;">)> {};
  struct Root
      : rule<create<ast::Won>(
            ((ref<NAME> + gap::none + assign<&ast::Won::token>(lit<"()">)) |
             (ref<ID> + assign<&ast::Won::name>(ref<ARGS>))) +
            lit<";">)> {};
  using entry = Root;
  using ignored = WS;
};

// The call and the arguments read the same name: one state holds both after
// it, and the refused `()` still reserves nothing from `ARGS`.
struct AttachedCallShared : AttachedCall {
  struct Root
      : rule<create<ast::Won>(
            ((ref<ID> + gap::none + assign<&ast::Won::token>(lit<"()">)) |
             (ref<ID> + assign<&ast::Won::name>(ref<ARGS>))) +
            lit<";">)> {};
  using entry = Root;
};

// The same name read by a rule: the token after it is read before the rule
// closes, and read again where the gap refuses the call.
struct AttachedCallCalled : AttachedCall {
  struct Callee : rule<some(ref<ID>)> {};
  struct Root
      : rule<create<ast::Won>(
            ((ref<Callee> + gap::none + assign<&ast::Won::token>(lit<"()">)) |
             (ref<Callee> + assign<&ast::Won::name>(ref<ARGS>))) +
            lit<";">)> {};
  using entry = Root;
};

struct Closers : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct TAG : terminal<through(lit<"<">, lit<">">) + lit<"x">> {};
  struct Root : rule<create<ast::Doc>(ref<TAG>)> {};
  using entry = Root;
  using ignored = WS;
};

struct Equals {};

struct LongString : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct LONG_STRING
      : terminal<through(lit<"["> + mark<Equals>(many(lit<"=">)) + lit<"[">,
                         lit<"]"> + same<Equals> + lit<"]">)> {};
  struct Root : rule<create<ast::Doc>(ref<LONG_STRING>)> {};
  using entry = Root;
  using ignored = WS;
};

struct Split : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root
      : rule<create<ast::Doc>((lit<"a"> + lit<"b">) | (lit<"ab"> + lit<"c">))> {
  };
  using entry = Root;
  using ignored = WS;
};

// `#(` opens an operand, which a caller reads after a whole expression, and
// reads over the `#` a call goes on with: the call takes `#` where the caller
// in hand reads no operand after it.
struct CallerLonger : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Expression;
  struct Primary : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>)) |
                        (lit<"#("> + ref<Expression> + lit<")">)> {};
  struct Expression
      : rule<ref<Primary> +
             many(nest<&ast::Call::callee>(
                 option(lit<"#">) + lit<"("> +
                 assign<&ast::Call::argument>(ref<Expression>) + lit<")">))> {};
  struct Root : rule<create<ast::Program>(
                    option(lit<"?"> +
                           append<&ast::Program::statements>(ref<Expression>)) +
                    append<&ast::Program::statements>(ref<Expression>))> {};
  using entry = Root;
  using ignored = WS;
};

struct CallerMany : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Inner : rule<many(lit<"x"> + lit<"a">)> {};
  struct Root
      : rule<create<ast::Doc>(ref<Inner> + option(lit<"x"> + lit<"ab"> + lit<"y">) +
                              option(lit<"b">)) +
             eof> {};
  using entry = Root;
  using ignored = WS;
};

struct UpToTwo : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root
      : rule<create<ast::Doc>(repeat<0, 2>(lit<"x"> + lit<"a">) +
                              option(lit<"x"> + lit<"ab"> + lit<"y">) +
                              option(lit<"b">)) +
             eof> {};
  using entry = Root;
  using ignored = WS;
};

// Two parses read `ab` apart: one as `a` then `b`, the other as one token.
struct TwoTokens : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct X : rule<create<ast::First>(lit<"x">)> {};
  struct Y : rule<create<ast::Second>(lit<"x">)> {};
  struct Root : rule<(ref<X> + lit<"q"> + lit<"a"> + lit<"b">) | (ref<Y> + lit<"q"> + lit<"ab">)> {};
  using entry = Root;
  using ignored = WS;
};

struct TwoTokensSwapped : TwoTokens {
  struct Root : rule<(ref<Y> + lit<"q"> + lit<"ab">) | (ref<X> + lit<"q"> + lit<"a"> + lit<"b">)> {};
  using entry = Root;
};

// Two parses read `if` apart: the first written as a name, the other as the keyword.
struct KeywordOrName : TwoTokens {
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Root : rule<(ref<Y> + lit<"q"> + ref<ID>) | (ref<X> + lit<"q"> + kw<"if">)> {};
  using entry = Root;
};

// Two parses read a line end apart: the first written skips it, the other
// reads it as a token.
struct LineEnd : TwoTokens {
  struct NL : terminal<eol> {};
  struct Root : rule<(ref<X> + lit<"q"> + lit<"a">) | (ref<Y> + lit<"q"> + ref<NL> + lit<"a">)> {};
  using entry = Root;
};

struct AtEnd : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root : rule<lit<"a"> + &eof> {};
  using entry = Root;
  using ignored = WS;
};

struct AtClose : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root : rule<lit<"a"> + &lit<"]">> {};
  using entry = Root;
  using ignored = WS;
};

struct Bare : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root : rule<lit<"a">> {};
  using entry = Root;
  using ignored = WS;
};

// A case-insensitive grammar a case-sensitive host embeds folds its own
// literals, and the host's stay as they are.
struct FoldedBare : cursiv::GrammarSpec {
  static constexpr bool case_insensitive = true;
  struct WS : terminal<some(s)> {};
  struct Root : rule<kw<"été">> {};
  using entry = Root;
  using ignored = WS;
};

struct AbsentClose : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root : rule<lit<"a"> + !lit<"]"> + &eof> {};
  using entry = Root;
  using ignored = WS;
};

struct Word : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Root : rule<create<ast::Leaf>(assign<&ast::Leaf::text>(ref<ID>))> {};
  using entry = Root;
  using ignored = WS;
};

template <class In>
struct Hosted : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root : rule<create<ast::Doc>(section<In>(lit<"[">, lit<"]">))> {};
  using entry = Root;
  using ignored = WS;
};

template <class In>
struct NamedClose : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct CLOSE : terminal<lit<"]">> {};
  struct Root : rule<create<ast::Doc>(section<In>(lit<"[">, ref<CLOSE>))> {};
  using entry = Root;
  using ignored = WS;
};

template <class In>
struct Followed : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root : rule<create<ast::Doc>(section<In>(lit<"[">, lit<"]">) +
                                      &lit<";"> + lit<";">)> {};
  using entry = Root;
  using ignored = WS;
};

template <class In>
struct Adopting : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root : rule<section<In>(lit<"[">, lit<"]">)> {};
  using entry = Root;
  using ignored = WS;
};

struct Calls : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  static constexpr auto semi = implied(lit<";">, gap::line | &lit<"}"> | eof);
  struct Primary : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>))> {
  };
  struct Expression
      : rule<ref<Primary> +
             many(nest<&ast::Call::callee>(
                 lit<"("> + assign<&ast::Call::argument>(ref<Expression>) +
                 lit<")">))> {};
  struct Root
      : rule<create<ast::Program>(
            many(append<&ast::Program::statements>(ref<Expression>) + semi))> {
  };
  using entry = Root;
  using ignored = WS;
};

// Statements stand side by side, and one may open on `(`: a run of calls
// after a member goes on as one after a name does.
struct MemberCalls : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Expression;
  struct Name : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>))> {};
  static constexpr auto calls = many(nest<&ast::Call::callee>(
      lit<"("> + assign<&ast::Call::argument>(ref<Expression>) + lit<")">));
  struct Expression
      : rule<(ref<Name> | (lit<"("> + ref<Expression> + lit<")">)) + calls +
             many(nest<&ast::Call::callee>(lit<"."> +
                                          assign<&ast::Call::argument>(ref<Name>)) +
                  calls)> {};
  struct Root : rule<create<ast::Program>(
                    many(append<&ast::Program::statements>(ref<Expression>)))> {};
  using entry = Root;
  using ignored = WS;
};


// `n.` the rule written without it. Cases 6 and 7 hold branches that stay.
struct Twins : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Name : rule<ref<ID>> {};
  struct Label : rule<ref<ID>> {};
  struct Root
      : rule<create<ast::Won>(
            (lit<"1"> + assign<&ast::Won::token>(ref<ID> | ref<ID>)) |
            (lit<"1."> + assign<&ast::Won::token>(ref<ID>)) |
            (lit<"2"> + (assign<&ast::Won::token>(ref<ID>) |
                         assign<&ast::Won::name>(ref<ID>))) |
            (lit<"2."> + assign<&ast::Won::token>(ref<ID>)) |
            (lit<"3"> + (assign<&ast::Won::token>(ref<Name>) |
                         assign<&ast::Won::name>(ref<Label>))) |
            (lit<"3."> + assign<&ast::Won::token>(ref<Name>)) |
            (lit<"4"> +
             ((assign<&ast::Won::token>(ref<ID>) + option(lit<";">)) |
              assign<&ast::Won::name>(ref<ID>))) |
            (lit<"4."> + assign<&ast::Won::token>(ref<ID>) +
             option(lit<";">)) |
            (lit<"5"> + option(assign<&ast::Won::token>(ref<ID>)) +
             option(assign<&ast::Won::name>(ref<ID>))) |
            (lit<"5."> + option(assign<&ast::Won::token>(ref<ID>) +
                                option(assign<&ast::Won::name>(ref<ID>)))) |
            (lit<"6"> + ((assign<&ast::Won::token>(ref<ID>) + lit<";">) |
                         assign<&ast::Won::name>(ref<ID>))) |
            (lit<"7"> + (assign<&ast::Won::token>(ref<ID>) |
                         (assign<&ast::Won::name>(ref<ID>) +
                          option(lit<";">)))))> {};
  using entry = Root;
  using ignored = WS;
};

// A state shifts `module` or closes the head; behind `b` the closed head goes
// on to a name, whose alphabet has no `module`. The lexer chooses one token for
// both readings, the keyword, wherever the statement stands in the text.
struct ClosedHead : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Head : rule<create<ast::Stated>(kw<"t"> +
                                         option(kw<"module"> + kw<"z">))> {};
  struct Keyed : rule<create<ast::Stated>(kw<"a"> +
                                          append<&ast::Stated::body>(ref<Head>) +
                                          kw<"module"> + kw<"y"> + lit<";">)> {};
  struct Named : rule<create<ast::Stated>(kw<"b"> +
                                          append<&ast::Stated::body>(ref<Head>) +
                                          assign<&ast::Stated::name>(ref<ID>) +
                                          lit<";">)> {};
  struct Root : rule<create<ast::Stated>(
                    many(append<&ast::Stated::body>(ref<Keyed> | ref<Named>)))> {};
  using entry = Root;
  using ignored = WS;
};

// Two statements share a head and a dotted name, behind a keyword each
// documents its own way, and are told apart by the word after the name.
struct SharedHead : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Path : rule<some(ref<ID>, lit<".">)> {};
  struct Root
      : rule<create<ast::Won>(kw<"ev">.doc<"An early event.">() +
                              assign<&ast::Won::token>(ref<Path>) + kw<"early">) |
             create<ast::Won>(kw<"ev">.doc<"A late event.">() +
                              assign<&ast::Won::name>(ref<Path>) + kw<"late">)> {};
  using entry = Root;
  using ignored = WS;
};

// The statements of `SharedHead` with names between the shared name and the
// word that tells them apart, and a third that is that keyword and a word.
struct SharedTags : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Path : rule<some(ref<ID>, lit<".">)> {};
  struct Root
      : rule<create<ast::Won>(kw<"ev">.doc<"An early event.">() +
                              assign<&ast::Won::token>(ref<Path>) + many(ref<ID>) +
                              kw<"early">) |
             create<ast::Won>(kw<"ev">.doc<"A late event.">() +
                              assign<&ast::Won::name>(ref<Path>) + many(ref<ID>) +
                              kw<"late">) |
             create<ast::First>(kw<"ev">.doc<"No event.">() + kw<"never">)> {};
  using entry = Root;
  using ignored = WS;
};

// Two statements read the same names behind a name each keeps its own way,
// the second alone on past its end mark behind a keyword; a third asks for a
// word after that mark.
struct ConvergedArms : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Root
      : rule<create<ast::Won>(kw<"ev"> + assign<&ast::Won::token>(ref<ID>) + many(ref<ID>) +
                              lit<";">) |
             create<ast::Won>(kw<"ev"> + assign<&ast::Won::name>(ref<ID>) + many(ref<ID>) +
                              option(kw<"upto"> + many(ref<ID> | lit<";">) + kw<"end">) +
                              lit<";">) |
             create<ast::First>(kw<"ev"> + some(ref<ID>) + lit<";"> + kw<"done">)> {};
  using entry = Root;
  using ignored = WS;
};

// Two lists read the same names, each name one of two ways, and are told
// apart by their last word.
struct AmbiguousList : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Item : rule<create<ast::Won>(assign<&ast::Won::token>(ref<ID>)) |
                     create<ast::Won>(assign<&ast::Won::name>(ref<ID>) + option(lit<"!">))> {};
  struct Early : rule<create<ast::Won>(assign<&ast::Won::token>(ref<ID>))> {};
  struct Late : rule<create<ast::Won>(assign<&ast::Won::name>(ref<ID>))> {};
  struct Root
      : rule<create<ast::Wons>(append<&ast::Wons::all>(ref<Early>) +
                               many(append<&ast::Wons::all>(ref<Item>)) + kw<"early">) |
             create<ast::Wons>(append<&ast::Wons::all>(ref<Late>) +
                               many(append<&ast::Wons::all>(ref<Item>)) + kw<"late">)> {};
  using entry = Root;
  using ignored = WS;
};

// Each level reads its block by either of two rules that read the same text,
// then goes on alike.
struct Levels : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct First : rule<create<ast::First>(lit<"{"> + lit<"}">)> {};
  struct Second : rule<create<ast::Second>(lit<"{"> + lit<"}">)> {};
  struct Root
      : rule<create<ast::Level>(
            kw<"r"> + ((assign<&ast::Level::first>(ref<First>) +
                        assign<&ast::Level::next>(ref<Root>)) |
                       (assign<&ast::Level::second>(ref<Second>) +
                        assign<&ast::Level::next>(ref<Root>)) |
                       kw<"e">))> {};
  using entry = Root;
  using ignored = WS;
};

// Each suffix of a chain is the operand's or the loop's, to the end of the
// text.
struct Chain : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Basic : rule<ref<ID> | (ref<Basic> + lit<"."> + ref<ID>)> {};
  struct Root : rule<create<ast::Doc>(ref<Basic> + many(lit<"."> + ref<ID>))> {};
  using entry = Root;
  using ignored = WS;
};

// A name is a run of characters that is no number and no capitalised word:
// the condition before it reads on without bound.
struct UnboundedName : cursiv::GrammarSpec {
  static constexpr auto NameChar = cr<"^ ,">;
  struct WS : terminal<some(s)> {};
  struct Name : terminal<!((some(d) | (cr<"A-Z"> + many(cr<"a-z">))) + !NameChar) +
                         some(NameChar)> {};
  struct Root : rule<create<ast::List>(some(append<&ast::List::items>(ref<Name>)))> {};
  using entry = Root;
  using ignored = WS;
};

// A keyword among more keywords of its first letter than a fault tries when
// the text reads on from none.
struct FaultEdges : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root : rule<create<ast::First>(kw<"i8"> | kw<"i16"> | kw<"i32"> | kw<"i64">) |
                    create<ast::Second>(kw<"include">)> {};
  using entry = Root;
  using ignored = WS;
};

// A rule whose test names a keyword and leads to a state that only reduces: a
// misspelt keyword is read there as the keyword it stands for.
struct NamedAhead : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct NUM : terminal<some(d)> {};
  struct Qualifier : rule<(ref<ID> + &kw<"then">) | ref<NUM>> {};
  struct Root : rule<create<ast::Doc>(ref<Qualifier> + kw<"then"> + ref<ID>)> {};
  using entry = Root;
  using ignored = WS;
};

// A rule whose test names a literal and leads to a state that only reduces: a
// literal left out there is put back where the test reads it.
struct InsertedAhead : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Head : rule<ref<ID> + &lit<".">> {};
  struct Root : rule<create<ast::Doc>(ref<Head> + lit<"."> + ref<ID> + lit<";">)> {};
  using entry = Root;
  using ignored = WS;
};

// Statements end at a semicolon, a line end, a closing brace or the end of
// the text, and a block in braces holds more of them. `go` ends a statement
// alone where no name follows it on its line.
struct Braced : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  static constexpr auto semi = implied(lit<";">, gap::line | &lit<"}"> | eof);
  struct Statement;
  struct Block : rule<lit<"{"> + many(ref<Statement>) + lit<"}">> {};
  struct Statement
      : rule<ref<Block> | (ref<ID> + semi) | (kw<"go"> + option(!gap::line + ref<ID>) + semi)> {};
  struct Root : rule<create<ast::Doc>(many(ref<Statement>))> {};
  using entry = Root;
  using ignored = WS;
};

// Fields in braces, each led by any number of tags: a field that has read no
// tag yet has not begun.
// A second name is a postfix operator only where a `)` closes it: a test that
// reads past the token in hand.
struct Postfixed : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Expr : rule<ref<ID> + option(ref<ID> + &(lit<")"> + !lit<"x">))> {};
  struct Root : rule<create<ast::Doc>(lit<"("> + ref<Expr> + lit<")"> + lit<";">)> {};
  using entry = Root;
  using ignored = WS;
};

// Three hundred symbols in one production, or a branch that needs more.
struct LongRun : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root : rule<create<ast::Doc>(
                    (repeat<300>(lit<"x">) + lit<";"> + many(lit<"n">)) |
                    (repeat<270>(lit<"x">) + lit<"k"> + lit<";"> + lit<"m"> + repeat<40>(lit<"n">)))> {};
  using entry = Root;
  using ignored = WS;
};

// A comment left open reads, where the lexer falls back, as a `/` and a `*`
// that an expression after an invented name could take. Each keyword waits
// for its own mark: readings in many modes reach the comment.
struct Divided : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct COMMENT : terminal<through(lit<"/*">, lit<"*/">)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Term : rule<ref<ID> | (lit<"*"> + ref<Term>)> {};
  struct Expr : rule<ref<Term> + many(lit<"/"> + ref<Term>)> {};
  struct Item : rule<(ref<Expr> + lit<";">) |
                     (kw<"ka"> + lit<"%a">) |
                     (kw<"kb"> + lit<"%b">) |
                     (kw<"kc"> + lit<"%c">) |
                     (kw<"kd"> + lit<"%d">) |
                     (kw<"ke"> + lit<"%e">) |
                     (kw<"kf"> + lit<"%f">) |
                     (kw<"kg"> + lit<"%g">) |
                     (kw<"kh"> + lit<"%h">) |
                     (kw<"ki"> + lit<"%i">) |
                     (kw<"kj"> + lit<"%j">) |
                     (kw<"kk"> + lit<"%k">) |
                     (kw<"kl"> + lit<"%l">) |
                     (kw<"km"> + lit<"%m">) |
                     (kw<"kn"> + lit<"%n">) |
                     (kw<"ko"> + lit<"%o">) |
                     (kw<"kp"> + lit<"%p">) |
                     (kw<"kq"> + lit<"%q">) |
                     (kw<"kr"> + lit<"%r">) |
                     (kw<"ks"> + lit<"%s">) |
                     (kw<"kt"> + lit<"%t">)> {};
  struct Root : rule<create<ast::Doc>(lit<"{"> + many(ref<Item>) + lit<"}">)> {};
  using entry = Root;
  using ignored = WS;
  using hidden = COMMENT;
};

// Fields open with a list of tags that reads nothing before their first word,
// and sixteen kinds give a repair many fields it could invent.
struct Slots : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct STRING : terminal<lit<"\""> + many(cr<"a-z0-9">) + lit<"\"">> {};
  struct Kind : rule<kw<"ka"> | kw<"kb"> | kw<"kc"> | kw<"kd"> | kw<"ke"> | kw<"kf"> | kw<"kg"> | kw<"kh"> |
                     kw<"ki"> | kw<"kj"> | kw<"kk"> | kw<"kl"> | kw<"km"> | kw<"kn"> | kw<"ko"> | kw<"kp">> {};
  struct Field : rule<many(lit<"@"> + ref<ID>) + kw<"o"> + ref<Kind> + ref<Kind> + ref<Kind> +
                      ref<Kind> + ref<ID> + option(kw<"default"> + lit<"="> + ref<STRING>) +
                      option(kw<"optional">)> {};
  struct Root : rule<create<ast::Doc>(lit<"{"> + many(ref<Field>) + lit<"}">)> {};
  using entry = Root;
  using ignored = WS;
};

// One document selects a conformance scenario before its original rule.
// Hidden-terminal policies are scoped to their own tagged cases.
struct Suite : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root
      : rule<(lit<"keyword:"> + ref<Keyword::Root>) |
             (lit<"keywordtie:"> + ref<KeywordTie::Root>) |
             (lit<"keywordtieboundary:"> + ref<KeywordTieBoundary::Root>) |
             (lit<"firstarm:"> + ref<FirstArm::Root>) |
             (lit<"elected:"> + ref<Elected::Root>) |
             (lit<"literalsrank:"> + ref<LiteralsRank::Root>) |
             (lit<"literalsrankrefuses:"> + ref<LiteralsRankRefuses::Root>) |
             (lit<"literalconditionrank:"> + ref<LiteralConditionRank::Root>) |
             (lit<"mixedrank:"> + ref<MixedRank::Root>) |
             (lit<"conditionedrank:"> + ref<ConditionedRank::Root>) |
             (lit<"conditionedrankswapped:"> +
              ref<ConditionedRankSwapped::Root>) |
             (lit<"patternstie:"> + ref<PatternsTie::Root>) |
             (lit<"patternstieswapped:"> + ref<PatternsTieSwapped::Root>) |
             (lit<"mixedrefused:"> + ref<MixedRefused::Root>) |
             (lit<"mixedleading:"> + ref<MixedLeading::Root>) |
             (lit<"literalbesideguard:"> + ref<LiteralBesideGuard::Root>) |
             (lit<"mergedlookahead:"> + ref<MergedLookahead::Root>) |
             (lit<"turn:"> + ref<Turn::Root>) |
             (lit<"turnshortfirst:"> + ref<TurnShortFirst::Root>) |
             (lit<"arms:"> + ref<Arms::Root>) |
             (lit<"caretguards:"> + ref<CaretGuards::Root>) |
             (lit<"fields:"> + ref<Fields::Root>) |
             (lit<"twins:"> + ref<Twins::Root>) |
             (lit<"separated:"> + ref<Separated::Root>) |
             (lit<"trailing:"> + ref<Trailing::Root>) |
             (lit<"grouped:"> + ref<Grouped::Root>) |
             (lit<"repeated:"> + ref<Repeated::Root>) |
             (lit<"tight:"> + ref<Tight::Root>) |
             (lit<"adjacent:"> + ref<Adjacent::Root>) |
             (lit<"attachedcall:"> + ref<AttachedCall::Root>) |
             (lit<"attachedcallshared:"> + ref<AttachedCallShared::Root>) |
             (lit<"attachedcallcalled:"> + ref<AttachedCallCalled::Root>) |
             (lit<"closers:"> + ref<Closers::Root>) |
             (lit<"longstring:"> + ref<LongString::Root>) |
             (lit<"split:"> + ref<Split::Root>) |
             (lit<"callerlonger:"> + ref<CallerLonger::Root>) |
             (lit<"callermany:"> + ref<CallerMany::Root>) |
             (lit<"uptotwo:"> + ref<UpToTwo::Root>) |
             (lit<"twotokens:"> + ref<TwoTokens::Root>) |
             (lit<"twotokensswapped:"> + ref<TwoTokensSwapped::Root>) |
             (lit<"keywordorname:"> + ref<KeywordOrName::Root>) |
             (lit<"lineend:"> + ref<LineEnd::Root>) |
             (lit<"atendhost:"> + ref<Hosted<AtEnd>::Root>) |
             (lit<"wordhost:"> + ref<Adopting<Word>::Root>) |
             (lit<"calls:"> + ref<Calls::Root>) |
             (lit<"membercalls:"> + ref<MemberCalls::Root>) |
             (lit<"atclosehost:"> + ref<Hosted<AtClose>::Root>) |
             (lit<"namedclosehost:"> + ref<NamedClose<AtClose>::Root>) |
             (lit<"namedendhost:"> + ref<NamedClose<AtEnd>::Root>) |
             (lit<"barehost:"> + ref<Hosted<Bare>::Root>) |
             (lit<"foldedhost:"> + ref<Hosted<FoldedBare>::Root>) |
             (lit<"absentclosehost:"> + ref<Hosted<AbsentClose>::Root>) |
             (lit<"followedhost:"> + ref<Followed<Bare>::Root>) |
             (lit<"closedhead:"> + ref<ClosedHead::Root>) |
             (lit<"sharedhead:"> + ref<SharedHead::Root>) |
             (lit<"sharedtags:"> + ref<SharedTags::Root>) |
             (lit<"faultedges:"> + ref<FaultEdges::Root>) |
             (lit<"namedahead:"> + ref<NamedAhead::Root>) |
             (lit<"insertedahead:"> + ref<InsertedAhead::Root>) |
             (lit<"braced:"> + ref<Braced::Root>) |
             (lit<"slots:"> + ref<Slots::Root>) |
             (lit<"postfixed:"> + ref<Postfixed::Root>) |
             (lit<"longrun:"> + ref<LongRun::Root>) |
             (lit<"divided:"> + skip(ignore<Divided::WS>, hide<Divided::COMMENT>,
                                     ref<Divided::Root> + eof)) |
             (lit<"convergedarms:"> + ref<ConvergedArms::Root>) |
             (lit<"ambiguouslist:"> + ref<AmbiguousList::Root>) |
             (lit<"levels:"> + ref<Levels::Root>) |
             (lit<"chain:"> + ref<Chain::Root>) |
             (lit<"unboundedname:"> + ref<UnboundedName::Root>) |
             (lit<"keywordtrivia:"> + skip(ignore<KeywordTrivia::WS>,
                                           hide<KeywordTrivia::NOTE>,
                                           ref<KeywordTrivia::Root> + eof)) |
             (lit<"literaltrivia:"> + skip(ignore<LiteralTrivia::WS>,
                                           hide<LiteralTrivia::NOTE>,
                                           ref<LiteralTrivia::Root> + eof)) |
             (lit<"conditionedtrivia:"> +
              skip(ignore<ConditionedTrivia::WS>, hide<ConditionedTrivia::NOTE>,
                   ref<ConditionedTrivia::Root> + eof)) |
             (lit<"fallbackkeyword:"> +
              skip(ignore<FallbackKeyword::WS, FallbackKeyword::COMMENT>, hide<>,
                   ref<FallbackKeyword::Root> + eof)) |
             (lit<"guardedliteral:"> + skip(ignore<GuardedLiteral::WS>,
                                            hide<GuardedLiteral::NOTE>,
                                            ref<GuardedLiteral::Root> + eof)) |
             (lit<"expectedkeyword:"> +
              skip(ignore<ExpectedKeyword::WS>, hide<ExpectedKeyword::REMARK>,
                   ref<ExpectedKeyword::Root> + eof)) |
             (lit<"foldedkeyword:"> + skip(ignore<FoldedKeyword::WS>,
                                           hide<FoldedKeyword::REMARK>,
                                           ref<FoldedKeyword::Root> + eof)) |
             ref<tcondcontract::Suite::Root>> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace tconformance
