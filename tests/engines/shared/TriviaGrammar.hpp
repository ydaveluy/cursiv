#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "engines/lr/BracketCaretGrammar.hpp"
#include "engines/shared/UnaryLevelsGrammar.hpp"
// What a terminal read as trivia is: two ignored terminals may read the same
// text, a literal outranks a tied pattern, one is no bracket of the
// indentation, and a newline token is trivia to a caller that reads none.

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
  using ignored = list<SPACE, BLANK>;
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
  using ignored = WS;
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
  using ignored = WS;
  using hidden = NOTE;
};

// An ignored blank that refuses two tabs at its head: two tabs inside a blank
// are trivia, and two that would open one are no token at all.
struct Refusing : cursiv::GrammarSpec {
  struct WS : terminal<!lit<"\t\t"> + some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Item : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>))> {};
  struct Root
      : rule<create<ast::Node>(many(append<&ast::Node::body>(ref<Item>)))> {};
  using entry = Root;
  using ignored = WS;
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
  using ignored = WS;
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


// Blanks of every kind are trivia, but between the words of a pair only
// spaces and tabs are: a newline may stand in front of a pair and behind it,
// where the caller reads the gap, and never inside it. Each pair stands in one
// place only, so none takes its first gap from another.

namespace tregion {

namespace ast {
struct Item : cursiv::Extends<cursiv::AstNode> {};
struct Pair : cursiv::Extends<Item> {
  string_view key;
  string_view value;
};
struct Group : cursiv::Extends<Item> {
  string_view kind;
  vector<Item*> items;
};
}  // namespace ast

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct HWS : terminal<some(cr<" \t">)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct NUM : terminal<some(d)> {};
  // First in one arm of a choice that a loop repeats; it opens on a choice.
  struct Pair : rule<skip(ignore<HWS>, hide<>,
                          assign<&ast::Pair::key>(ref<ID> | ref<NUM>) +
                              assign<&ast::Pair::value>(ref<ID> | ref<NUM>))> {
  };
  struct Block
      : rule<create<ast::Group>(assign<&ast::Group::kind>(lit<"{">) +
                                many(append<&ast::Group::items>(ref<Pair>) |
                                     append<&ast::Group::items>(ref<Block>)) +
                                lit<"}">)> {};
  // The whole turn of a loop, and first in a sequence; it opens on a terminal.
  struct Word : rule<skip(ignore<HWS>, hide<>,
                          assign<&ast::Pair::key>(ref<ID>) + lit<"="> +
                              assign<&ast::Pair::value>(ref<ID>))> {};
  struct List
      : rule<create<ast::Group>(assign<&ast::Group::kind>(lit<"[">) +
                                many(append<&ast::Group::items>(ref<Word>)) +
                                lit<"]">)> {};
  struct Line : rule<create<ast::Group>(append<&ast::Group::items>(ref<Word>) +
                                        assign<&ast::Group::kind>(lit<";">))> {
  };
  // An arm of a choice no loop repeats.
  struct Duo : rule<skip(ignore<HWS>, hide<>,
                         assign<&ast::Pair::key>(ref<ID>) +
                             assign<&ast::Pair::value>(ref<NUM>))> {};
  struct Pick : rule<create<ast::Group>(
                    assign<&ast::Group::kind>(lit<"?">) +
                    (append<&ast::Group::items>(ref<Duo>) |
                     append<&ast::Group::items>(ref<Block>)))> {};
  // A region that opens on a loop: the gap in front of its first turn, or of
  // its `>` where it has none, is the caller's; the others are its own.
  struct Atom : rule<create<ast::Pair>(assign<&ast::Pair::key>(ref<ID>))> {};
  struct Run : rule<create<ast::Group>(
                   assign<&ast::Group::kind>(lit<"<">) +
                   skip(ignore<HWS>, hide<>,
                        many(append<&ast::Group::items>(ref<Atom>)) +
                            lit<">">))> {};
  struct Root : rule<create<ast::Group>(
                    many(append<&ast::Group::items>(ref<Block>) |
                         append<&ast::Group::items>(ref<List>) |
                         append<&ast::Group::items>(ref<Line>) |
                         append<&ast::Group::items>(ref<Pick>) |
                         append<&ast::Group::items>(ref<Run>)))> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace tregion

// The same law on the shape of a map file: the trivia of an entity's body is
// every blank and a hidden comment, a key and its value share one line, and
// a word reads any text that is no blank, braces included.

namespace tleading {

namespace ast {
struct KeyValue : cursiv::Extends<cursiv::AstNode> {
  string_view key;
  string_view value;
};
struct Brush : cursiv::Extends<cursiv::AstNode> {};
struct Entity : cursiv::Extends<cursiv::AstNode> {
  vector<KeyValue*> keyvalues;
  vector<Brush*> brushes;
};
struct Document : cursiv::Extends<cursiv::AstNode> {
  vector<Entity*> entities;
};
}  // namespace ast

inline constexpr auto unquote = [](std::string_view t) {
  return t.substr(1, t.size() - 2);
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"(\x01-\x20)">)> {};
  struct HWS : terminal<some(cr<R"(\x01-\x09\x0b-\x20)">)> {};
  struct COMMENT : terminal<lit<"//"> + many(cr<"^\n">)> {};
  static constexpr auto token_end = &(cr<R"(\x01-\x20)"> | eof);
  struct TEXT
      : terminal<!lit<"//"> + !lit<"\""> + some(cr<R"(^\x00-\x20)">)> {};
  struct STRING : terminal<as<unquote>(lit<"\""> + many(cr<R"(^")">) +
                                       lit<"\"">)> {};
  struct KeyValue
      : rule<skip(ignore<HWS>, hide<>,
                  assign<&ast::KeyValue::key>(ref<STRING> | ref<TEXT>) +
                      assign<&ast::KeyValue::value>(ref<STRING> |
                                                    ref<TEXT>))> {};
  struct Brush : rule<create<ast::Brush> + lit<"{", token_end> +
                      lit<"}", token_end>> {};
  struct Entity
      : rule<lit<"{", token_end> +
             many(append<&ast::Entity::keyvalues>(ref<KeyValue>) |
                  append<&ast::Entity::brushes>(ref<Brush>)) +
             lit<"}", token_end>> {};
  struct Document
      : rule<create<ast::Document> +
             many(append<&ast::Document::entities>(ref<Entity>))> {};
  using entry = Document;
  using ignored = WS;
  using hidden = list<COMMENT>;
};
}  // namespace tleading

namespace ttrivia {
struct Suite : cursiv::GrammarSpec {
  struct Entry
      : rule<(lit<"tied:"> + skip(ignore<Tied::SPACE, Tied::BLANK>, hide<>,
                                  ref<Tied::Root> + eof)) |
             (lit<"ranked:"> + skip(ignore<Ranked::WS>, hide<Ranked::NOTE>,
                                    ref<Ranked::Root> + eof)) |
             (lit<"bracketed:"> + skip(ignore<Bracketed::WS>,
                                       hide<Bracketed::NOTE>,
                                       ref<Bracketed::Root> + eof)) |
             (lit<"refusing:"> + skip(ignore<Refusing::WS>, hide<>,
                                      ref<Refusing::Root> + eof)) |
             (lit<"implied:"> + skip(ignore<timpliedtag::Spelled::WS>, hide<>,
                                     ref<timpliedtag::Suite::Root> + eof)) |
             skip(ignore<tregion::Base::WS>, hide<>,
                  lit<"region:"> + ref<tregion::Base::Root> + eof) |
             skip(ignore<tleading::Base::WS>, hide<tleading::Base::COMMENT>,
                  lit<"leading:"> + ref<tleading::Base::Document> + eof) |
             (lit<"lines:"> + skip(ignore<tunary::Lines::WS, tunary::Lines::NL>,
                                   hide<>, ref<tunary::Lines::Root> + eof))> {};
  using entry = Entry;
  static constexpr auto indent_open = lit<"(">;
  static constexpr auto indent_close = lit<")">;
};
struct Ll : Suite {};
// Newline completion without INDENT/DEDENT shares this bracket-aware table.
struct Lr : Suite {
  struct Entry
      : rule<ref<Suite::Entry> |
             skip(ignore<tbracketcaret::G::WS, tbracketcaret::G::NL>, hide<>,
                  lit<"bracket:"> + ref<tbracketcaret::G::Root> + eof)> {};
  using entry = Entry;
  using indent_newline = tbracketcaret::G::NEWLINE;
  static constexpr auto indent_open = tbracketcaret::G::indent_open;
  static constexpr auto indent_close = tbracketcaret::G::indent_close;
};
}  // namespace ttrivia
