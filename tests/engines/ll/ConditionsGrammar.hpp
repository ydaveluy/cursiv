#pragma once

#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"

namespace tllconditions {

struct Document : cursiv::Extends<cursiv::AstNode> {
  vector<cursiv::AstNode*> items;
};
struct Expression : cursiv::Extends<cursiv::AstNode> {};
struct Number : cursiv::Extends<Expression> {};
struct Binary : cursiv::Extends<Expression> {
  Expression* left{};
  string_view op;
  Expression* right{};
};
struct StatementNode : cursiv::Extends<cursiv::AstNode> {
  Expression* value{};
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct COMMENT : terminal<through(lit<"/*">, lit<"*/">)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct NUMBER : terminal<some(d)> {};
  struct Atom : rule<create<Number>(ref<NUMBER>)> {};
  struct SameLineAdd : rule<!gap::line + lit<"+">> {};
  struct Sum : infix<Atom, Binary, left(ref<SameLineAdd>)> {};
  static constexpr auto Eos = implied(lit<";">, gap::line | &lit<"}"> | eof);
  // Where a condition refuses the longest token: `->` is a terminal read behind
  // a gap, or past a line's end.
  struct ArrowTail : rule<(lit<"-"> + lit<">"> + ref<ID>) | gap::line> {};
  struct Mark : rule<option(lit<"%">)> {};
  // A condition on `~>`, a spelling no terminal declares: at the head of an
  // arm, behind a nullable rule, on a turn of a loop, on a climb's operand.
  struct Waved : rule<create<Number>(!lit<"~>"> + option(lit<"~">) +
                                     ref<NUMBER>)> {};
  struct WavedSum : infix<Waved, Binary, left(lit<"*">)> {};
  struct Statement
      : rule<create<StatementNode>(
            (lit<"gapexit:"> + ref<ID> + ref<ArrowTail> +
             option(lit<"->"> + ref<ID>)) |
            (lit<"gaparm:"> + ref<ID> +
             ((gap::none + lit<"->"> + ref<ID>) |
              (lit<"-"> + lit<">"> + ref<ID>))) |
            (lit<"gapnull:"> + ((ref<Mark> + gap::none + lit<"->"> + ref<ID>) |
                                (lit<"-"> + lit<">"> + ref<ID>))) |
            (lit<"gapmany:"> + ((gap::none + lit<"->"> + ref<ID>) |
                                (ref<Mark> + gap::line + lit<"->"> + ref<ID>) |
                                (lit<"-"> + lit<">"> + ref<ID>))) |
            (lit<"andloop:"> + many(&lit<"a"> + (ref<ID> | lit<"a">)) +
             lit<";">) |
            (lit<"andhead:"> + &lit<"a"> + (ref<ID> | lit<"a">) + ref<ID>) |
            (lit<"andpair:"> +
             many(&(lit<"a"> + lit<"b">) + (ref<ID> | lit<"a">) + lit<"b">) +
             lit<";">) |
            (lit<"atarm:"> + ((option(lit<"%">) + !lit<"~>"> +
                               option(lit<"~">) + ref<ID>) |
                              (lit<"~"> + lit<">">))) |
            (lit<"atcall:"> + ((ref<Mark> + !lit<"~>"> + option(lit<"~">) +
                                ref<ID>) |
                               (lit<"~"> + lit<">">))) |
            (lit<"atshared:"> + ((ref<Mark> + !lit<"~>"> + option(lit<"~">) +
                                  ref<ID>) |
                                 (ref<Mark> + lit<"~"> + lit<">">))) |
            (lit<"atturn:"> + many(!lit<"~>"> + lit<"~"> + ref<ID>) +
             option(lit<"~"> + lit<">">)) |
            (lit<"atclimb:"> + (assign<&StatementNode::value>(ref<WavedSum>) |
                                (lit<"~"> + lit<">">))) |
            (kw<"return"> + option(!gap::line + ref<ID>) + Eos) |
            (assign<&StatementNode::value>(ref<Sum>) + Eos) |
            (!kw<"reserved"> + ref<ID> +
             many(gap::none + lit<"."> + gap::none + ref<ID>) + Eos))> {};
  struct Block : rule<create<Document>(
                     lit<"{"> + many(append<&Document::items>(ref<Statement>)) +
                     lit<"}">)> {};
  // Arms that open alike, one waiting on the gap in front of its second word.
  struct OnLine : rule<create<StatementNode>(ref<ID> + gap::line + kw<"over"> +
                                             lit<";">)> {};
  struct SameLine : rule<create<StatementNode>(ref<ID> + kw<"under"> +
                                               lit<";">)> {};
  // `->` reads over the `-` a statement goes on with, and opens the next
  // statement: a gap decides which of the two a stack takes.
  struct Tail : rule<(lit<"-"> + lit<">"> + ref<ID> + lit<";">) | gap::line> {};
  struct LineEnded
      : rule<create<StatementNode>(kw<"sv"> + ref<ID> + ref<Tail>)> {};
  struct Tight : rule<create<StatementNode>(
                     kw<"sz"> + ref<ID> +
                     ((gap::none + lit<"->"> + ref<ID> + lit<";">) |
                      (lit<"-"> + lit<">"> + ref<ID> + lit<";">)))> {};
  struct Arrow : rule<create<StatementNode>(lit<"->"> + ref<ID> + lit<";">)> {};
  // Alternatives alike past their first token, which conditions tell apart.
  struct Fork : rule<create<StatementNode>(
                    (kw<"fk"> + ref<ID> + gap::none + lit<";">) |
                    (kw<"fk"> + ref<ID> + lit<"("> + lit<")">) |
                    (kw<"fk"> + ref<ID> + &lit<"x"> + lit<"x"> + lit<";">))> {};
  // Nesting behind an operand that no condition stands in: the rest of the
  // grammar has conditions, this choice meets none.
  struct NestPrimary;
  struct NestPostfix : rule<(ref<NestPrimary> + lit<"!">) | ref<NestPrimary>> {};
  struct NestSum : rule<ref<NestPostfix> + many(lit<"+"> + ref<NestPostfix>)> {};
  struct NestPrimary : rule<ref<NUMBER> | (lit<"("> + ref<NestSum> + lit<")">)> {};
  // An operator the text may leave out: the right operand is read all the same.
  struct Times : infix<Atom, Binary, left(implied(lit<"*">, &ref<NUMBER>))> {};
  struct Timed : rule<create<StatementNode>(assign<&StatementNode::value>(ref<Times>))> {};
  struct Root
      : rule<create<Document>(
            (many(append<&Document::items>(ref<Block> | ref<Statement>)) +
             eof) |
            (lit<"nest:"> + ref<NestSum> + eof) |
            (lit<"times:"> + append<&Document::items>(ref<Timed>) + eof) |
            (lit<"arrow:"> +
             many(append<&Document::items>(ref<LineEnded> | ref<Tight> |
                                           ref<Arrow> | ref<Fork>)) +
             eof) |
            (lit<"arms:"> +
             append<&Document::items>(ref<OnLine> | ref<SameLine>) + eof))> {};
  using entry = Root;
  using ignored = WS;
  using hidden = COMMENT;
};

struct Ll : Base {};
struct Lr : Base {};

struct TriviaBase : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct COMMENT : terminal<through(lit<"/*">, lit<"*/">)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  // This visible reading is used only in the leading region. It must not
  // hide the grammar's ignored whitespace during later prediction.
  struct LeadingSpace : terminal<some(s)> {};
  struct GuardedName : rule<!kw<"reserved"> + ref<ID>> {};
  struct Names : rule<ref<GuardedName> + many(ref<GuardedName>)> {};
  struct Definition : rule<create<StatementNode>(
                          (ref<Names> + lit<"="> + ref<GuardedName>) |
                          (ref<Names> + lit<":"> + ref<GuardedName>))> {};
  struct KeywordDefinition : rule<create<StatementNode>(kw<"get"> + lit<"?">)> {
  };
  struct Root : rule<create<Document>(
                    option(tight(ref<LeadingSpace>)) +
                    many(append<&Document::items>(ref<Definition> |
                                                  ref<KeywordDefinition>) +
                         lit<";">) +
                    eof)> {};
  using entry = Root;
  using ignored = WS;
  using hidden = COMMENT;
};
struct TriviaLl : TriviaBase {};

// The condition of an implied `;` reads more of the word after it than the
// token a prediction lexes there: `abd` refuses the insertion only at its
// third byte.
struct ReachBase : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Implied : rule<lit<"x"> + implied(lit<";">, &lit<"abc">)> {};
  struct Word : rule<lit<"abc">> {};
  struct Letters : rule<lit<"x"> + lit<"a"> + lit<"b"> + lit<"d">> {};
  struct Statement : rule<ref<Implied> | ref<Word> | ref<Letters>> {};
  struct Root : rule<create<Document>(many(ref<Statement>))> {};
  using entry = Root;
  using ignored = WS;
};
struct ReachLl : ReachBase {};

// An implied `;` whose condition holds on a token another reading takes
// normally: after a name, `#` continues `a #b?`, which blocks the insertion
// even where that reading fails later. After a number, nothing reads `#`.
struct RivalBase : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct NUMBER : terminal<some(d)> {};
  static constexpr auto Mark =
      lit<"#"> + ref<ID> + ref<ID> + ref<ID> + lit<"!">;
  static constexpr auto Eos = implied(lit<";">, &Mark);
  struct Statement
      : rule<create<StatementNode>(
            (ref<ID> + Eos) | (ref<NUMBER> + Eos) | Mark |
            (ref<ID> + lit<"#"> + ref<ID> + lit<"?">) | (ref<ID> + ref<ID>))> {
  };
  struct Root : rule<create<Document>(
                    many(append<&Document::items>(ref<Statement>)) + eof)> {};
  using entry = Root;
  using ignored = WS;
};
struct RivalLl : RivalBase {};
struct RivalLr : RivalBase {};

// A tag glued to its number: one creating group of `#`, a gap condition and
// an option, whose node rides onto the `#` where nothing follows it.
struct GluedBase : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NUMBER : terminal<some(d)> {};
  struct Atom : rule<create<Number>(ref<NUMBER>)> {};
  struct Tag : rule<create<StatementNode>(
                   lit<"#"> + gap::none +
                   option(assign<&StatementNode::value>(ref<Atom>)))> {};
  struct Root : rule<create<Document>(
                    many(append<&Document::items>(ref<Tag> | ref<Atom>)) +
                    eof)> {};
  using entry = Root;
  using ignored = WS;
};
struct GluedLl : GluedBase {};
}  // namespace tllconditions
