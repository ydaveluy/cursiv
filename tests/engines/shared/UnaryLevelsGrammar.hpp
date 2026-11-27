#pragma once

#include <cstdint>
#include <string_view>

#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"

// Prefix and postfix levels in a precedence table: each applies to everything
// that binds tighter than it, a prefix may begin any operand and a postfix may
// end any operand.

namespace tunary {

enum class Step : std::uint8_t { inc, dec, bang };

namespace ast {
struct Expr : cursiv::Extends<cursiv::AstNode> {};
struct Name : cursiv::Extends<Expr> {
  string_view name;
};
struct Binary : cursiv::Extends<Expr> {
  Expr* left = nullptr;
  string_view op;
  Expr* right = nullptr;
};
struct Unary : cursiv::Extends<Expr> {
  string_view op;
  Expr* operand = nullptr;
};
// Declares no member of its own: its base declares the two a level writes.
struct Signed : cursiv::Extends<Unary> {};
struct Not : cursiv::Extends<Expr> {
  string_view word;
  Expr* body = nullptr;
};
struct Postfix : cursiv::Extends<Expr> {
  Expr* operand = nullptr;
  Step op = Step::inc;
};
struct Wrapped : cursiv::Extends<Expr> {
  Expr* value = nullptr;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  Expr* value = nullptr;
};
struct Block : cursiv::Extends<Expr> {
  vector<Expr*> items;
};
// A block between braces, whose line ends count inside brackets that mute them.
struct Braced : cursiv::Extends<Block> {};
struct Lines : cursiv::Extends<cursiv::AstNode> {
  vector<Expr*> items;
};
}  // namespace ast

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  using ignored = WS;
};

// The class forms, in an `infix<>` table.
struct Table : Base {
  struct E;
  struct Primary : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>)) |
                        (lit<"("> + ref<E> + lit<")">)> {};
  struct E : infix<Primary, ast::Binary,
                   levels(postfix<ast::Postfix>(as<Step::inc>(lit<"++">) |
                                                as<Step::dec>(lit<"--">)),
                          prefix<ast::Signed>(lit<"-"> | lit<"!">),
                          left(lit<"*"> | lit<"/">), left(lit<"+"> | lit<"-">),
                          nonassoc(lit<"<"> | lit<">">),
                          prefix<&ast::Not::word, &ast::Not::body>(option(lit<"~">) + kw<"NOT">),
                          left(kw<"AND">), left(kw<"OR">))> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::value>(ref<E>)) + eof> {
  };
};

// A table its primary reads again at its end: every climb of a chain of bodies
// ends on the token after the last one.
struct Bodies : Base {
  struct E;
  struct Primary : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>)) |
                        create<ast::Wrapped>(
                            kw<"fn"> + assign<&ast::Wrapped::value>(ref<E>))> {
  };
  struct E : infix<Primary, ast::Binary, left(lit<"+">)> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::value>(ref<E>)) +
                     lit<";"> + eof> {};
};

// The member forms, in an `infix<>` table, with a postfix level looser than a
// binary one.
struct Members : Base {
  struct E;
  struct Primary : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>)) |
                        (lit<"("> + ref<E> + lit<")">)> {};
  struct E
      : infix<Primary, ast::Binary,
              levels(prefix<&ast::Unary::op, &ast::Unary::operand>(lit<"~">),
                     left(lit<"&">),
                     postfix<&ast::Postfix::operand, &ast::Postfix::op>(
                         as<Step::bang>(lit<"!">)))> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::value>(ref<E>)) + eof> {
  };
};

// A primary that begins with the token a prefix level reads.
struct Signs : Base {
  struct Name : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>))> {};
  struct Primary
      : rule<ref<Name> | create<ast::Wrapped>(
                             lit<"-"> + assign<&ast::Wrapped::value>(ref<Name>))> {
  };
  struct E : infix<Primary, ast::Binary,
                   levels(prefix<ast::Signed>(lit<"-">), left(lit<"*">))> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::value>(ref<E>)) + eof> {
  };
};

// A table of a prefix level alone writes no left operand: a rule that takes
// its node over writes one.
struct Adopted : Base {
  struct Name : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>))> {};
  struct Word : rule<create<ast::Binary>(assign<&ast::Binary::op>(ref<ID>))> {};
  struct E : infix<Word, ast::Binary, prefix<&ast::Binary::op, &ast::Binary::right>(lit<"-">)> {};
  struct Left : rule<ref<E> + lit<"<"> + assign<&ast::Binary::left>(ref<Name>)> {};
  // Its operand members are no binary level's: one member may name both.
  struct Same : infix<Word, ast::Binary, prefix<&ast::Binary::op, &ast::Binary::right>(lit<"!">),
                      &ast::Binary::right, &ast::Binary::op, &ast::Binary::right> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::value>(ref<Left> | ref<Same>)) + eof> {};
};

// Binary operators that read on within a line only: a line that opens with
// one begins a statement of its own.
struct Gapped : Base {
  struct Name : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>))> {};
  static constexpr auto sign = lit<"+"> | lit<"-">;
  template <auto Op>
  struct Lined : rule<!gap::line + Op> {};
  struct E;
  static constexpr auto eos = implied(lit<";">, gap::line | &lit<"}"> | eof);
  struct Primary
      : rule<ref<Name> |
             create<ast::Block>(lit<"["> + many(append<&ast::Block::items>(ref<E>)) + lit<"]">) |
             create<ast::Braced>(lit<"{"> + many(append<&ast::Block::items>(ref<E>) + eos) +
                                 lit<"}">)> {};
  struct E : infix<Primary, ast::Binary,
                   levels(prefix<ast::Signed>(sign), left(ref<Lined<lit<"*">>>),
                          left(ref<Lined<sign>>), nonassoc(ref<Lined<lit<"<">>>))> {};
  struct Root : rule<create<ast::Lines>(many(append<&ast::Lines::items>(ref<E>) + eos)) + eof> {};
};

// The same operators, their guard read through a rule of its own.
struct Factored : Gapped {
  struct SameLine : rule<!gap::line> {};
  template <auto Op>
  struct Lined : rule<ref<SameLine> + Op> {};
  struct E : infix<Name, ast::Binary,
                   levels(prefix<ast::Signed>(sign), left(ref<Lined<lit<"*">>>),
                          left(ref<Lined<sign>>), nonassoc(ref<Lined<lit<"<">>>))> {};
  struct Root : rule<create<ast::Lines>(many(append<&ast::Lines::items>(ref<E>) + eos)) + eof> {};
};

// The same operators, read on their operand's line or after a blank: a guard
// of two alternatives, written in place, then through a rule of its own.
struct Either : Gapped {
  template <auto Op>
  struct Lined : rule<((!gap::line) | gap::has(lit<" ">)) + Op> {};
  struct E : infix<Name, ast::Binary,
                   levels(prefix<ast::Signed>(sign), left(ref<Lined<lit<"*">>>),
                          left(ref<Lined<sign>>), nonassoc(ref<Lined<lit<"<">>>))> {};
  struct Root : rule<create<ast::Lines>(many(append<&ast::Lines::items>(ref<E>) + eos)) + eof> {};
};
struct EitherRule : Gapped {
  struct OnLine : rule<(!gap::line) | gap::has(lit<" ">)> {};
  template <auto Op>
  struct Lined : rule<ref<OnLine> + Op> {};
  struct E : infix<Name, ast::Binary,
                   levels(prefix<ast::Signed>(sign), left(ref<Lined<lit<"*">>>),
                          left(ref<Lined<sign>>), nonassoc(ref<Lined<lit<"<">>>))> {};
  struct Root : rule<create<ast::Lines>(many(append<&ast::Lines::items>(ref<E>) + eos)) + eof> {};
};

// A table its primary reaches at its left edge.
struct Cyclic : Base {
  struct E;
  struct Primary : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>)) |
                        (lit<"("> + ref<E> + lit<")">) |
                        create<ast::Wrapped>(
                            assign<&ast::Wrapped::value>(ref<E>) + lit<"?">)> {
  };
  struct E : infix<Primary, ast::Binary,
                   levels(postfix<ast::Postfix>(as<Step::inc>(lit<"++">)),
                          right(lit<"**">), prefix<ast::Signed>(lit<"-">),
                          left(lit<"*">))> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::value>(ref<E>)) + eof> {
  };
};

// An operator two gap tests guard: on its operand's line, after a blank.
struct Spaced : Base {
  struct Name : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>))> {};
  struct Minus : rule<!gap::line + gap::has(lit<" ">) + lit<"-">> {};
  struct E : infix<Name, ast::Binary,
                   levels(prefix<ast::Signed>(lit<"-">), left(ref<Minus>), left(lit<"*">))> {};
  static constexpr auto eos = implied(lit<";">, gap::line | eof);
  struct Root : rule<create<ast::Lines>(many(append<&ast::Lines::items>(ref<E>) + eos)) + eof> {};
};

struct GuardedTurns : Base {
  struct Atom : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID> | d))> {};
  struct Direct : rule<ref<Atom> | create<ast::Wrapped>(
      &ref<ID> + assign<&ast::Wrapped::value>(ref<Direct>) + lit<"?">)> {};
  struct Compound : rule<ref<Atom> | create<ast::Wrapped>(
      (&kw<"if"> | &kw<"else">) + assign<&ast::Wrapped::value>(ref<Compound>) + lit<"?">)> {};
  struct E;
  struct Primary : rule<ref<Atom> | create<ast::Wrapped>(
      (&ref<ID> | gap::line) + assign<&ast::Wrapped::value>(ref<E>) + lit<"?">)> {};
  struct E : infix<Primary, ast::Binary, left(lit<"+">)> {};
  struct Gap : rule<ref<Atom> | create<ast::Wrapped>(
      gap::line + assign<&ast::Wrapped::value>(ref<Gap>) + lit<"!">)> {};
  struct Word : rule<ref<Atom> | create<ast::Wrapped>(
      &kw<"if"> + assign<&ast::Wrapped::value>(ref<Word>) + lit<"$">) |
      create<ast::Wrapped>(&kw<"else"> + assign<&ast::Wrapped::value>(ref<Word>) + lit<"!">)> {};
  struct Scoped : rule<ref<Atom> | create<ast::Wrapped>(
      skip(ignore<>, hide<>, !kw<"if">) + assign<&ast::Wrapped::value>(ref<Scoped>) + lit<"?">)> {};
  struct Overlap : rule<(option(lit<"!">) + ref<Atom>) | create<ast::Wrapped>(
      &ref<ID> + assign<&ast::Wrapped::value>(ref<Overlap>) + lit<"?">)> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::value>(
      (lit<"direct:"> + ref<Direct>) | (lit<"compound:"> + ref<Compound>) |
      (lit<"infix:"> + ref<E>) |
      (lit<"gap:"> + ref<Gap>) | (lit<"word:"> + ref<Word>) | (lit<"scoped:"> + ref<Scoped>) |
      (lit<"overlap:"> + option(lit<"!">) + ref<Overlap>))) + eof> {};
};

inline constexpr auto elvis_text = [] { return std::string_view("?:"); };
inline constexpr auto and_text = [] { return std::string_view("&&"); };
inline constexpr auto or_text = [] { return std::string_view("||"); };

// A newline read as a token ends a statement; elsewhere it is trivia. A
// member or `?:` may open a line and an index may not, so an index on the next
// line starts a statement.
struct Lines : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t)">)> {};
  // A line end before a member is a blank.
  struct NEWLINE : terminal<eol + !(many(cr<R"( \t)">) + lit<".">)> {};
  struct NL : terminal<eol> {};
  struct NOTE : terminal<lit<"/*"> + many(cr<"^*">) + lit<"*/">> {};
  static constexpr auto NLs = many(ref<NEWLINE>);
  struct E;
  struct Name
      : rule<create<ast::Name>(assign<&ast::Name::name>(ref<Base::ID>))> {};
  struct Atom
      : rule<ref<Name> |
             create<ast::Block>(lit<"["> + many(append<&ast::Block::items>(ref<E>)) + lit<"]">) |
             create<ast::Braced>(lit<"{"> + NLs +
                                 option(some(append<&ast::Block::items>(ref<E>),
                                             some(ref<NEWLINE>)) +
                                        NLs) +
                                 lit<"}">)> {};
  struct Postfix
      : rule<ref<Atom> +
            many(nest<ast::Binary, &ast::Binary::left>(
                (assign<&ast::Binary::op>(lit<"[">) +
                 assign<&ast::Binary::right>(ref<E>) + lit<"]">) |
                (assign<&ast::Binary::op>(lit<".">) +
                 assign<&ast::Binary::right>(ref<Name>))))> {};
  struct Elvis : rule<as<elvis_text>(NLs + lit<"?:">)> {};
  struct Chain : infix<Postfix, ast::Binary, left(ref<Elvis>)> {};
  struct E : infix<Chain, ast::Binary, right(lit<"=">)> {};
  // Two levels of one table whose operators may open a line.
  struct Both : infix<Name, ast::Binary,
                      levels(left(as<and_text>(NLs + lit<"&&">)),
                             left(as<or_text>(NLs + lit<"||">)))> {};
  struct Logic
      : rule<create<ast::Lines>(
            NLs +
            option(some(append<&ast::Lines::items>(ref<Both>),
                        some(ref<NEWLINE>)) +
                   NLs))> {};
  struct Entry
      : rule<create<ast::Binary>(
            (assign<&ast::Binary::left>(ref<E>) +
             assign<&ast::Binary::op>(lit<"->">) +
             assign<&ast::Binary::right>(ref<E>) + NLs) |
            (assign<&ast::Binary::op>(kw<"else">) + lit<"->"> +
             assign<&ast::Binary::right>(ref<E>)))> {};
  struct When
      : rule<create<ast::Block>(kw<"when"> + NLs +
                                many(append<&ast::Block::items>(ref<Entry>)) +
                                kw<"end">)> {};
  struct Root
      : rule<create<ast::Lines>(
            NLs +
            option(some(append<&ast::Lines::items>(ref<When> | ref<E>),
                        some(ref<NEWLINE>)) +
                   NLs))> {};
  // One rule reads the name for every arm, so the readings that completed it
  // share the newline, whichever call of it they made: a bracket on the next
  // line starts a statement of its own.
  struct Head : rule<create<ast::Name>(assign<&ast::Name::name>(ref<Base::ID>))> {
  };
  struct Box : rule<create<ast::Block>(lit<"["> +
                                       many(append<&ast::Block::items>(ref<E>)) +
                                       lit<"]">)> {};
  struct Minus : rule<create<ast::Binary>(assign<&ast::Binary::left>(ref<Head>) +
                                          assign<&ast::Binary::right>(ref<Box>) +
                                          assign<&ast::Binary::op>(lit<"-">) +
                                          ref<NEWLINE>)> {};
  struct Bare : rule<ref<Head> + ref<NEWLINE>> {};
  struct Plus : rule<create<ast::Binary>(assign<&ast::Binary::left>(ref<Head>) +
                                         assign<&ast::Binary::right>(ref<Box>) +
                                         assign<&ast::Binary::op>(lit<"+">) +
                                         ref<NEWLINE>)> {};
  struct Loose : rule<create<ast::Binary>(assign<&ast::Binary::right>(ref<Box>) +
                                          assign<&ast::Binary::op>(lit<"-">) +
                                          ref<NEWLINE>)> {};
  struct Heads : rule<create<ast::Lines>(many(append<&ast::Lines::items>(
                     ref<Minus> | ref<Bare> | ref<Plus> | ref<Loose>)))> {};
  // Blocks that only their ends tell apart, nested, under heads that rules of
  // their own read: what a level costs does not grow with the levels over it.
  struct Nested;
  static constexpr auto body =
      lit<":"> + ref<NEWLINE> + lit<"{"> +
      some(append<&ast::Block::items>(ref<Nested>)) + lit<"}">;
  struct HeadOf : rule<create<ast::Name>(assign<&ast::Name::name>(ref<Base::ID>))> {
  };
  struct Nested
      : rule<create<ast::Block>(
            (append<&ast::Block::items>(ref<Head>) + body + lit<"!"> +
             ref<NEWLINE>) |
            (append<&ast::Block::items>(ref<HeadOf>) + body + lit<"?"> +
             ref<NEWLINE>) |
            (append<&ast::Block::items>(ref<Name>) + ref<NEWLINE>))> {};
  struct Blocks : rule<create<ast::Lines>(many(append<&ast::Lines::items>(
                      ref<Nested>)))> {};
  // Line ends of their own, one reading the `b` after it: a comment's line end
  // is neither.
  struct JOINED : terminal<eol + lit<"b">> {};
  struct EOL : terminal<eol> {};
  struct Joined : rule<create<ast::Lines>(append<&ast::Lines::items>(ref<Name>) +
                                          (ref<JOINED> | (ref<EOL> + lit<".">)) +
                                          append<&ast::Lines::items>(ref<Name>))> {};
  // Two arms an empty rule of each opens: the first reads a line end between
  // its names, which a comment's line end stands for.
  struct Ends : rule<create<ast::Block>> {};
  struct Runs : rule<create<ast::Braced>> {};
  static constexpr auto item = [](auto of) { return append<&ast::Lines::items>(of); };
  struct Told : rule<create<ast::Lines>(
                    (item(ref<Ends>) + item(ref<Name>) + ref<NEWLINE> + item(ref<Name>)) |
                    (item(ref<Runs>) + item(ref<Name>) + item(ref<Name>)))> {};
  // Two postfix levels a bracket opens, and one alone: inside them a line
  // end is a blank, which a test of the gap does not see.
  struct Indexed : infix<Name, ast::Binary,
                         levels(postfix<&ast::Unary::operand, &ast::Unary::op>(
                                    lit<"["> + ref<Base::ID> + !gap::line + lit<"]">),
                                postfix<&ast::Unary::operand, &ast::Unary::op>(
                                    lit<"["> + ref<Base::ID> + lit<"!"> + !gap::line + lit<"]">))> {};
  struct Once : infix<Name, ast::Binary,
                      postfix<&ast::Unary::operand, &ast::Unary::op>(
                          lit<"["> + ref<Base::ID> + !gap::line + lit<"]">)> {};
  struct Indexes : rule<create<ast::Lines>(
                       NLs + option(some(item(ref<Indexed> | (lit<"once"> + ref<Once>)),
                                         some(ref<NEWLINE>)) +
                                    NLs))> {};
};

// Rules that may read nothing, nested two by two, which only what follows them
// tells apart: one closure walks every way through them.
struct Empties : Base {
  static constexpr auto item = [](auto of) {
    return append<&ast::Block::items>(of);
  };
  struct Empty0 : rule<create<ast::Block>(option(kw<"a">))> {};
#define TUNARY_TWICE(k, j)                                    \
  struct Empty##k : rule<create<ast::Block>(item(ref<Empty##j>) + \
                                            item(ref<Empty##j>))> {};
  TUNARY_TWICE(1, 0) TUNARY_TWICE(2, 1) TUNARY_TWICE(3, 2) TUNARY_TWICE(4, 3)
  TUNARY_TWICE(5, 4) TUNARY_TWICE(6, 5) TUNARY_TWICE(7, 6) TUNARY_TWICE(8, 7)
  TUNARY_TWICE(9, 8) TUNARY_TWICE(10, 9) TUNARY_TWICE(11, 10)
  TUNARY_TWICE(12, 11) TUNARY_TWICE(13, 12) TUNARY_TWICE(14, 13)
  TUNARY_TWICE(15, 14) TUNARY_TWICE(16, 15)
#undef TUNARY_TWICE
  struct Nest : rule<create<ast::Block>(lit<"("> + many(item(ref<Nest>)) +
                                        lit<")">)> {};
  struct Semi : rule<create<ast::Block>(item(ref<Empty16>) +
                                        option(item(ref<Nest>)) + lit<";">)> {};
  struct Bang : rule<create<ast::Block>(item(ref<Empty16>) +
                                        option(item(ref<Nest>)) + lit<"!">)> {};
  struct Root : rule<ref<Semi> | ref<Bang>> {};
  // Sixteen of them, which the stacks of a caret's closure merge.
  struct Few : rule<create<ast::Block>(item(ref<Empty4>) + lit<";">)> {};
};

struct Suite : Base {
  struct Root : rule<(lit<"unary:"> + ref<Table::Root>) |
                     (lit<"bodies:"> + ref<Bodies::Root>) |
                     (lit<"members:"> + ref<Members::Root>) |
                     (lit<"cyclic:"> + ref<Cyclic::Root>) |
                     (lit<"signs:"> + ref<Signs::Root>) |
                     (lit<"adopted:"> + ref<Adopted::Root>) |
                     (lit<"gapped:"> + ref<Gapped::Root>) |
                     (lit<"factored:"> + ref<Factored::Root>) |
                     (lit<"either:"> + ref<Either::Root>) |
                     (lit<"eitherrule:"> + ref<EitherRule::Root>) |
                     (lit<"spaced:"> + ref<Spaced::Root>) |
                     (lit<"guarded:"> + ref<GuardedTurns::Root>) |
                     (lit<"lines:"> + skip(ignore<Lines::WS, Lines::NL>, hide<Lines::NOTE>,
                                           ref<Lines::Root> + eof)) |
                     (lit<"heads:"> + skip(ignore<Lines::WS, Lines::NL>, hide<>,
                                           ref<Lines::Heads> + eof)) |
                     (lit<"logic:"> + skip(ignore<Lines::WS, Lines::NL>, hide<Lines::NOTE>,
                                           ref<Lines::Logic> + eof)) |
                     (lit<"empties:"> + ref<Empties::Root> + eof) |
                     (lit<"few:"> + ref<Empties::Few> + eof) |
                     (lit<"blocks:"> + skip(ignore<Lines::WS, Lines::NL>, hide<>,
                                            ref<Lines::Blocks> + eof)) |
                     (lit<"glued:"> + skip(ignore<Lines::WS, Lines::NL>, hide<Lines::NOTE>,
                                            ref<Lines::Joined> + eof)) |
                     (lit<"told:"> + skip(ignore<Lines::WS, Lines::NL>, hide<Lines::NOTE>,
                                          ref<Lines::Told> + eof)) |
                     (lit<"indexed:"> + skip(ignore<Lines::WS, Lines::NL>, hide<>,
                                             ref<Lines::Indexes> + eof))> {};
  using entry = Root;
  using hidden = Lines::NOTE;
  using indent_newline = Lines::NEWLINE;
  static constexpr auto indent_newline_in = ref<Lines::NOTE>;
  static constexpr auto indent_open = lit<"[">;
  static constexpr auto indent_block = lit<"{">;
  static constexpr auto indent_close = lit<"]"> | lit<"}">;
};

// The member table alone, for a fuzz lane: its language is part of the
// suite's, so the suite's parsers read what it writes.
struct Fuzzed : Base {
  struct Root : rule<lit<"members:"> + ref<Members::Root>> {};
  using entry = Root;
};

}  // namespace tunary
