#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "engines/lr/BracketCaretGrammar.hpp"
// What a terminal read as trivia is: two ignored terminals may read the same
// text, a literal outranks a tied pattern, and one is no bracket of the
// indentation.

namespace ttrivia {

namespace ast {
struct Node : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  vector<Node*> body;
};
}  // namespace ast

struct Tied : cursiv::GrammarSpec {
  struct SPACE : terminal<some(s)> {};
  struct BLANK : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Item : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>))> {};
  struct Root
      : rule<create<ast::Node>(many(append<&ast::Node::body>(ref<Item>)))> {};
  using entry = Root;
  using ignore = list<SPACE, BLANK>;
};

struct Bracketed : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NOTE : terminal<lit<"(">> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Item
      : rule<create<ast::Node>(
            assign<&ast::Node::name>(ref<ID>) +
            option(lit<"("> + many(append<&ast::Node::body>(ref<Item>)) +
                   lit<")">))> {};
  struct Root
      : rule<create<ast::Node>(many(append<&ast::Node::body>(ref<Item>)))> {};
  using entry = Root;
  using ignore = WS;
  using hidden = NOTE;
  static constexpr auto indent_open = lit<"(">;
  static constexpr auto indent_close = lit<")">;
};

struct Ranked : cursiv::GrammarSpec {
  struct WS : terminal<some(s) | lit<"/*@egen*/">> {};
  struct NOTE : terminal<lit<"/*"> + many(cr<"^*">) + lit<"*/">> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Item : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>))> {};
  struct Root
      : rule<create<ast::Node>(many(append<&ast::Node::body>(ref<Item>)))> {};
  using entry = Root;
  using ignore = WS;
  using hidden = NOTE;
};


}  // namespace ttrivia

// A closing tag that opens on an operator: where the implied `;` may stand,
// `?>` lexes whole for the condition to read, and is never taken as `?`.

namespace timpliedtag {

namespace ast {
struct Stmt : cursiv::Extends<cursiv::AstNode> {
  string_view head;
  vector<Stmt*> args;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Stmt*> body;
};
}  // namespace ast

struct Spelled : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Expr;
  struct Expr : rule<create<ast::Stmt>(
                    assign<&ast::Stmt::head>(ref<ID>) +
                    option(lit<"?"> + append<&ast::Stmt::args>(ref<Expr>) +
                           lit<":"> + append<&ast::Stmt::args>(ref<Expr>)))> {};
  struct Stmt : rule<ref<Expr> + implied(lit<";">, &lit<"?>">)> {};
  struct Doc
      : rule<create<ast::Doc>(
            lit<"<?"> + many(append<&ast::Doc::body>(ref<Stmt>)) + lit<"?>">)> {
  };
  using entry = Doc;
  using ignore = WS;
};

struct Named : Spelled {
  struct CLOSE : terminal<lit<"?>">> {};
  struct Stmt : rule<ref<Expr> + implied(lit<";">, &ref<CLOSE>)> {};
  struct Doc : rule<create<ast::Doc>(lit<"<?"> +
                                     many(append<&ast::Doc::body>(ref<Stmt>)) +
                                     ref<CLOSE>)> {};
  using entry = Doc;
};

// The statements end on two tokens, and `?>` is a token of another form only:
// the condition names it, and the state before the implied `;` lexes it.
struct Apart : Spelled {
  struct Doc : rule<create<ast::Doc>((lit<"<?"> +
                                      many(append<&ast::Doc::body>(ref<Stmt>)) +
                                      lit<"?"> + lit<">">) |
                                     (lit<"<%"> + lit<"?>">))> {};
  using entry = Doc;
};

struct Suite : Spelled {
  struct Root : rule<ref<Spelled::Doc> |
                     (lit<"named:"> + ref<Named::Doc>) |
                     (lit<"apart:"> + ref<Apart::Doc>)> {};
  using entry = Root;
};

}  // namespace timpliedtag

namespace ttrivia {
struct Suite : cursiv::GrammarSpec {
  struct Entry
      : rule<(lit<"tied:"> + skip(ignored<Tied::SPACE, Tied::BLANK>, hide<>,
                                  ref<Tied::Root> + eof)) |
             (lit<"ranked:"> + skip(ignored<Ranked::WS>, hide<Ranked::NOTE>,
                                    ref<Ranked::Root> + eof)) |
             (lit<"bracketed:"> + skip(ignored<Bracketed::WS>,
                                       hide<Bracketed::NOTE>,
                                       ref<Bracketed::Root> + eof)) |
             (lit<"implied:"> + skip(ignored<timpliedtag::Spelled::WS>, hide<>,
                                     ref<timpliedtag::Suite::Root> + eof))> {};
  using entry = Entry;
  static constexpr auto indent_open = lit<"(">;
  static constexpr auto indent_close = lit<")">;
};
struct Ll : Suite {};
// Newline completion without INDENT/DEDENT shares this bracket-aware table.
struct Lr : Suite {
  struct Entry
      : rule<ref<Suite::Entry> |
             skip(ignored<tbracketcaret::G::WS, tbracketcaret::G::NL>, hide<>,
                  lit<"bracket:"> + ref<tbracketcaret::G::Root> + eof)> {};
  using entry = Entry;
  using indent_newline = tbracketcaret::G::NEWLINE;
  static constexpr auto indent_open = tbracketcaret::G::indent_open;
  static constexpr auto indent_close = tbracketcaret::G::indent_close;
};
}  // namespace ttrivia
