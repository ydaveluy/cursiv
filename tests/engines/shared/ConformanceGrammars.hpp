#pragma once

// The conformance cases of the grammar reference, one grammar each. Letters
// are distinct tokens, blanks are skipped and the whole input is read.

#include "cursiv/core/grammar/Conversion.hpp"
#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
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
struct Tally : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> turns;
  bool tail = false;
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

// One grammar for the cases the election leaves to the grammar's order, each
// behind a digit: the keyword whose continuation fails, a keyword against a
// name with and without a lookahead, a repetition's turn before an option,
// and a conversion that refuses the elected reading.
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
  struct Root : rule<create<ast::Pick>(
                    (assign<&ast::Pick::literal>(ref<LIT_IF>) + lit<"(">) |
                    (assign<&ast::Pick::keyword>(ref<MIX>) + lit<"(">))> {};
  using entry = Root;
  using ignored = WS;
};

struct MixedRefused : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NAME : terminal<some(cr<"a-zA-Z">)> {};
  struct MIX : terminal<(lit<"if"> | ref<NAME>) + !cr<"0-9">> {};
  struct Root : rule<create<ast::Won>(
                    (assign<&ast::Won::name>(ref<NAME>) + lit<";">) |
                    (assign<&ast::Won::token>(ref<MIX>) + lit<";">))> {};
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

struct Ladder : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root
      : rule<create<ast::Doc>(repeat<0, 2>(lit<"x"> + lit<"a">) +
                              option(lit<"x"> + lit<"ab"> + lit<"y">) +
                              option(lit<"b">)) +
             eof> {};
  using entry = Root;
  using ignored = WS;
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

// A branch, or an optional item, that reads what an earlier one reads is never
// chosen, so the generator drops it. Case `n` holds the twin, and case `n.`
// the rule written without it. Case 6 holds branches that only look alike,
// and case 7 a branch that reads one text of an earlier one and more.
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
             (lit<"twins:"> + ref<Twins::Root>) |
             (lit<"separated:"> + ref<Separated::Root>) |
             (lit<"trailing:"> + ref<Trailing::Root>) |
             (lit<"grouped:"> + ref<Grouped::Root>) |
             (lit<"repeated:"> + ref<Repeated::Root>) |
             (lit<"tight:"> + ref<Tight::Root>) |
             (lit<"adjacent:"> + ref<Adjacent::Root>) |
             (lit<"closers:"> + ref<Closers::Root>) |
             (lit<"longstring:"> + ref<LongString::Root>) |
             (lit<"split:"> + ref<Split::Root>) |
             (lit<"callermany:"> + ref<CallerMany::Root>) |
             (lit<"ladder:"> + ref<Ladder::Root>) |
             (lit<"atendhost:"> + ref<Hosted<AtEnd>::Root>) |
             (lit<"wordhost:"> + ref<Adopting<Word>::Root>) |
             (lit<"calls:"> + ref<Calls::Root>) |
             (lit<"atclosehost:"> + ref<Hosted<AtClose>::Root>) |
             (lit<"namedclosehost:"> + ref<NamedClose<AtClose>::Root>) |
             (lit<"namedendhost:"> + ref<NamedClose<AtEnd>::Root>) |
             (lit<"barehost:"> + ref<Hosted<Bare>::Root>) |
             (lit<"absentclosehost:"> + ref<Hosted<AbsentClose>::Root>) |
             (lit<"followedhost:"> + ref<Followed<Bare>::Root>) |
             (lit<"keywordtrivia:"> + skip(ignore<KeywordTrivia::WS>,
                                           hide<KeywordTrivia::NOTE>,
                                           ref<KeywordTrivia::Root> + eof)) |
             (lit<"literaltrivia:"> + skip(ignore<LiteralTrivia::WS>,
                                           hide<LiteralTrivia::NOTE>,
                                           ref<LiteralTrivia::Root> + eof)) |
             (lit<"conditionedtrivia:"> +
              skip(ignore<ConditionedTrivia::WS>,
                   hide<ConditionedTrivia::NOTE>,
                   ref<ConditionedTrivia::Root> + eof)) |
             (lit<"guardedliteral:"> + skip(ignore<GuardedLiteral::WS>,
                                            hide<GuardedLiteral::NOTE>,
                                            ref<GuardedLiteral::Root> + eof)) |
             (lit<"expectedkeyword:"> +
              skip(ignore<ExpectedKeyword::WS>, hide<ExpectedKeyword::REMARK>,
                   ref<ExpectedKeyword::Root> + eof)) |
             ref<tcondcontract::Suite::Root>> {};
  using entry = Root;
  using ignored = WS;
};
struct Ll : Suite {};
struct Lr : Suite {};

}  // namespace tconformance
