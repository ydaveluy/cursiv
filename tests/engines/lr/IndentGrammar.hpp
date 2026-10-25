#pragma once

// Blocks by indentation: a statement's suite opens on a deeper line and
// closes on a line back at an enclosing level, one dedent per level.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "engines/lr/VirtualStateGrammar.hpp"

namespace tindent {

namespace ast {
struct Node : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  vector<Node*> body;
};
}  // namespace ast

struct Plain : cursiv::GrammarSpec {
  // A backslash continues the line: the next physical line starts no
  // logical one.
  struct WS : terminal<some(cr<R"( \t\f)">) | (lit<"\\"> + eol)> {};
  // A blank line ends no statement: absorbed, so the next line's run is
  // read at the next token. Declared before the trivia newline, so where a
  // state reads both the same byte is the statement's end.
  struct NEWLINE : terminal<eol + many(many(cr<R"( \t)">) + eol)> {};
  struct NL : terminal<eol> {};
  // A backslash ending a comment is the comment's: the next line is a line.
  struct COMMENT : terminal<lit<"#"> + many(cr<R"(^\r\n)">)> {};
  struct INDENT : terminal<indent> {};
  struct DEDENT : terminal<dedent> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Stmt;
  // A bracketed list too: inside it the indentation says nothing and a
  // newline is a blank, the statement's own newline included.
  struct Item : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>))> {};
  struct Stmt : rule<create<ast::Node>(
                    assign<&ast::Node::name>(ref<ID>) +
                    ((lit<":"> + ref<NEWLINE> + ref<INDENT> +
                      some(append<&ast::Node::body>(ref<Stmt>)) +
                      ref<DEDENT>) |
                     (lit<"["> + some(append<&ast::Node::body>(ref<Item>)) +
                      lit<"]"> + (ref<NEWLINE> | eof)) |
                     (ref<NEWLINE> | eof)))> {};
  struct Root : rule<create<ast::Node>(
                    many(append<&ast::Node::body>(ref<Stmt>)))> {};
  using entry = Root;
  using ignore = list<WS, NL>;
  using hidden = list<COMMENT>;
  static constexpr auto indent_open = lit<"[">;
  static constexpr auto indent_close = lit<"]">;
  using indent_newline = NEWLINE;
};

// The newline carries the lines of comments as it carries the blank ones, so
// the line the indentation reads is a line of content whatever a comment
// line's run says; a comment ends the statement it follows.
namespace folded {
struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t)">)> {};
  struct NEWLINE
      : terminal<eol + many(many(cr<R"( \t)">) +
                            option(lit<"#"> + many(cr<R"(^\r\n)">)) + eol)> {};
  struct COMMENT : terminal<lit<"#"> + many(cr<R"(^\r\n)">)> {};
  using INDENT = Plain::INDENT;
  using DEDENT = Plain::DEDENT;
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Stmt;
  struct Stmt : rule<create<ast::Node>(
                    assign<&ast::Node::name>(ref<ID>) +
                    ((lit<":"> + ref<NEWLINE> + ref<INDENT> +
                      some(append<&ast::Node::body>(ref<Stmt>)) + ref<DEDENT>) |
                     (ref<NEWLINE> | eof)))> {};
  struct Root
      : rule<create<ast::Node>(many(append<&ast::Node::body>(ref<Stmt>)))> {};
  using entry = Root;
  using ignore = WS;
  using hidden = list<COMMENT>;
  using indent_newline = NEWLINE;
};
}  // namespace folded

namespace repaired {
// Three-byte delimiters can be re-read with two unchanged bytes and one
// typo. The following statement must still see its own newline and level.
struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t)">)> {};
  struct NEWLINE : terminal<eol> {};
  struct NL : terminal<eol> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Stmt;
  struct Item : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>))> {};
  struct Stmt : rule<create<ast::Node>(
                    assign<&ast::Node::name>(ref<ID>) +
                    ((lit<":"> + ref<NEWLINE> + indent +
                      some(append<&ast::Node::body>(ref<Stmt>)) + dedent) |
                     (lit<"[[["> + some(append<&ast::Node::body>(ref<Item>)) +
                      lit<"]]]"> + (ref<NEWLINE> | eof)) |
                     (ref<NEWLINE> | eof)))> {};
  struct Root : rule<create<ast::Node>(
                    many(append<&ast::Node::body>(ref<Stmt>)))> {};
  using entry = Root;
  using ignore = list<WS, NL>;
  static constexpr auto indent_open = lit<"[[[">;
  static constexpr auto indent_close = lit<"]]]">;
  using indent_newline = NEWLINE;
};
}  // namespace repaired

// Suites of names, and a last line whose newline the end of input stands for.
// Behind their own opening keyword, suites of keywords: the caret on a suite's
// first line stands glued to the indent's run, which no lexer reads.
namespace laid {
struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t)">)> {};
  struct NEWLINE : terminal<eol + many(many(cr<R"( \t)">) + eol)> {};
  struct COMMENT : terminal<lit<"#"> + many(cr<R"(^\r\n)">)> {};
  using INDENT = Plain::INDENT;
  using DEDENT = Plain::DEDENT;
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Stmt;
  struct Stmt : rule<create<ast::Node>(
                    assign<&ast::Node::name>(ref<ID>) +
                    ((lit<":"> + ref<NEWLINE> + ref<INDENT> +
                      some(append<&ast::Node::body>(ref<Stmt>)) + ref<DEDENT>) |
                     ref<NEWLINE> | eof))> {};
  struct Inner : rule<create<ast::Node>(kw<"pass"> + ref<NEWLINE>)> {};
  struct Top : rule<create<ast::Node>(
                   kw<"top"> + assign<&ast::Node::name>(ref<ID>) + lit<":"> +
                   ref<NEWLINE> + ref<INDENT> +
                   some(append<&ast::Node::body>(ref<Inner>)) + ref<DEDENT>)> {};
  struct Root : rule<create<ast::Node>(
                    many(append<&ast::Node::body>(ref<Stmt>)))> {};
  struct Tops : rule<create<ast::Node>(
                    many(append<&ast::Node::body>(ref<Top>)))> {};
  struct Case : rule<ref<Root> | (kw<"suites"> + ref<Tops>)> {};
  using entry = Case;
  using ignore = WS;
  using hidden = list<COMMENT>;
};
}  // namespace laid

namespace joined {
struct Words : cursiv::GrammarSpec {
  using WS = Plain::WS;
  using NEWLINE = Plain::NEWLINE;
  using NL = Plain::NL;
  using INDENT = Plain::INDENT;
  using DEDENT = Plain::DEDENT;
  using ID = Plain::ID;
};
}  // namespace joined

// One reading freezes before the newline. The other's indent covers bytes,
// but its newline and name are only two real validation tokens: at EOF the
// earlier reading can still repair one semicolon rather than three closers.
namespace validation {
struct G : joined::Words {
  using WS = folded::G::WS;
  using NEWLINE = folded::G::NEWLINE;
  struct First : rule<flag<&tvirtual::Node::first>(lit<"x">)> {};
  struct Second : rule<flag<&tvirtual::Node::second>(lit<"x">)> {};
  struct Root
      : rule<(ref<First> + many(lit<"a">) + lit<";"> + ref<NEWLINE> +
              ref<INDENT> + ref<ID> + ref<DEDENT>) |
             (ref<Second> + many(lit<"a">) + ref<NEWLINE> + ref<INDENT> +
              ref<ID> + lit<"!"> + lit<"@"> + lit<"$"> + ref<DEDENT>)> {};
  using entry = Root;
  using ignore = WS;
  using indent_newline = NEWLINE;
};
}  // namespace validation

}  // namespace tindent

// Two readings of one bracketed name the byte after the bracket tells apart:
// a statement's newline, or the name a statement goes on with.
namespace tvirtual::indented {
struct G : tindent::joined::Words {
  struct First : rule<flag<&Node::first>(lit<"x">)> {};
  struct Second : rule<flag<&Node::second>(lit<"x">)> {};
  struct Root
      : rule<(lit<"["> + ((ref<First> + lit<"]"> + ref<NEWLINE> + ref<ID>) |
                          (ref<Second> + lit<"]"> + ref<ID> + lit<"!">))) |
             create<Node>(lit<"suite"> + ref<NEWLINE> + indent + ref<ID> +
                          dedent)> {};
};
}  // namespace tvirtual::indented

namespace tindent {

// A list holds expressions a statement also ends with: the state after one
// reads the statement's newline, which inside the brackets is a blank.
// Beside them, a bracket and a suite one rule opens and its caller closes:
// each opener builds its own type, so its record names one rule and the
// caret steps over it whole.
namespace joined {
struct Bracket : cursiv::Extends<ast::Node> {};
struct Suite : cursiv::Extends<ast::Node> {};
struct G : Words {
  struct Expr;
  struct Expr : rule<create<ast::Node>(
                    assign<&ast::Node::name>(ref<ID>) +
                    option(lit<"["> +
                           many(append<&ast::Node::body>(ref<Expr>),
                                lit<",">) +
                           lit<"]">))> {};
  struct Stmt;
  struct Stmt : rule<create<ast::Node>(
                    append<&ast::Node::body>(ref<Expr>) +
                    ((lit<":"> + ref<NEWLINE> + ref<INDENT> +
                      some(append<&ast::Node::body>(ref<Stmt>)) +
                      ref<DEDENT>) |
                     ref<NEWLINE>))> {};
  struct Open : rule<create<Bracket>(lit<"["> +
                                     assign<&ast::Node::name>(ref<ID>))> {};
  struct Head : rule<create<Suite>(kw<"block"> +
                                   assign<&ast::Node::name>(ref<ID>) +
                                   lit<":"> + ref<NEWLINE> + ref<INDENT>)> {};
  struct Split;
  struct Split : rule<create<ast::Node>(
                     (append<&ast::Node::body>(ref<Open>) + lit<","> +
                      (kw<"pass"> | (ref<NEWLINE> + kw<"other">)) +
                      lit<"]"> + ref<NEWLINE>) |
                     (append<&ast::Node::body>(ref<Head>) +
                      some(append<&ast::Node::body>(ref<Split>)) +
                      ref<DEDENT>) |
                     (kw<"pass"> + ref<NEWLINE>))> {};
  struct Root : rule<create<ast::Node>(
                    many(append<&ast::Node::body>(ref<Stmt> | ref<Split>)))> {};
  // Behind its keyword, the bracketed name above.
  struct Top
      : rule<ref<Root> | (kw<"virt"> + ref<tvirtual::indented::G::Root>)> {};
  using entry = Top;
  using ignore = list<WS, NL>;
  static constexpr auto indent_open = lit<"[">;
  static constexpr auto indent_close = lit<"]">;
  using indent_newline = NEWLINE;
};
}  // namespace joined

// A bracket a condition guards: `a[b]` is a list, `a [b]` refuses the
// bracket, which then opens nothing.
namespace guarded {
struct G : joined::Words {
  struct Stmt;
  struct Item : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>))> {};
  struct Stmt : rule<create<ast::Node>(
                    assign<&ast::Node::name>(ref<ID>) +
                    ((lit<":"> + ref<NEWLINE> + ref<INDENT> +
                      some(append<&ast::Node::body>(ref<Stmt>)) +
                      ref<DEDENT>) |
                     (gap::none + lit<"["> +
                      some(append<&ast::Node::body>(ref<Item>)) + lit<"]"> +
                      (ref<NEWLINE> | eof)) |
                     (ref<NEWLINE> | eof)))> {};
  struct Root : rule<create<ast::Node>(
                    many(append<&ast::Node::body>(ref<Stmt>)))> {};
  using entry = Root;
  using ignore = list<WS, NL>;
  static constexpr auto indent_open = lit<"[">;
  static constexpr auto indent_close = lit<"]">;
  using indent_newline = NEWLINE;
};
}  // namespace guarded

// A line of words and its line break end a paragraph, or open a definition
// where the next line is deeper: the two readings share the line break, and
// only the indentation after it tells them apart.
namespace fork {
struct Term : cursiv::Extends<ast::Node> {};
struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t)">)> {};
  struct EOL : terminal<eol + many(many(cr<R"( \t)">) + eol)> {};
  using INDENT = Plain::INDENT;
  using DEDENT = Plain::DEDENT;
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Text : rule<some(ref<ID>)> {};
  struct Key : rule<some(ref<ID>)> {};
  struct Item;
  struct Para : rule<create<ast::Node>(assign<&ast::Node::name>(ref<Text>) +
                                       ref<EOL>)> {};
  struct Def : rule<create<Term>(assign<&ast::Node::name>(ref<Key>) +
                                 ref<EOL> + ref<INDENT> +
                                 some(append<&ast::Node::body>(ref<Item>)) +
                                 ref<DEDENT>)> {};
  struct Item : rule<ref<Def> | ref<Para>> {};
  struct Root : rule<create<ast::Node>(
                    many(append<&ast::Node::body>(ref<Item>)))> {};
  using entry = Root;
  using ignore = WS;
};
}  // namespace fork

// Three bracket and newline cases use the same lexical policy.
struct G : Plain {
  struct Case : rule<ref<Plain::Root> |
                     (kw<"joined"> + ref<joined::G::Top>) |
                     (kw<"guarded"> + ref<guarded::G::Root>)> {};
  using entry = Case;
};

namespace folded {
struct Cases : G {
  struct Case : rule<ref<G::Root> |
                     (kw<"validation"> + ref<validation::G::Root>)> {};
  using entry = Case;
};
}  // namespace folded

namespace laid {
struct Cases : G {
  struct Case : rule<ref<G::Case> | (kw<"fork"> + ref<fork::G::Root>)> {};
  using entry = Case;
};
}  // namespace laid

}  // namespace tindent
