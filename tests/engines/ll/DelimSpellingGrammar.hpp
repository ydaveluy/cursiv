#pragma once

// The brackets the indentation is blind inside of, using literals, spelled
// terminals, named terminals and a multi-byte opener in one language.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tdelim {
namespace ast {
struct Node : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  vector<Node*> body;
};
}  // namespace ast

// Each delimiter family exercises a different spelling of the same indentation
// contract: inline punctuation, named tokens selected by their spelling,
// named tokens selected by reference, and a multi-byte opener.
struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t)">)> {};
  struct NEWLINE : terminal<eol + many(many(cr<R"( \t)">) + eol)> {};
  struct INDENT : terminal<indent> {};
  struct DEDENT : terminal<dedent> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct LBRACK : terminal<lit<"[">> {};
  struct RBRACK : terminal<lit<"]">> {};
  struct LBRACE : terminal<lit<"{">> {};
  struct RBRACE : terminal<lit<"}">> {};
  struct LANGLE : terminal<lit<"<"> + option(lit<"~">)> {};
  struct RANGLE : terminal<lit<">">> {};
  struct Dotted : rule<ref<ID> + many(many(ref<NEWLINE>) + lit<"."> + ref<ID>)> {
  };
  struct Item
      : rule<create<ast::Node>(assign<&ast::Node::name>(ref<Dotted>))> {};
  struct Stmt;
  struct Stmt : rule<create<ast::Node>(
                    assign<&ast::Node::name>(ref<ID>) +
                    ((lit<":"> + ref<NEWLINE> + ref<INDENT> +
                      some(append<&ast::Node::body>(ref<Stmt>)) +
                      ref<DEDENT>) |
                     (lit<"("> + some(append<&ast::Node::body>(ref<Item>)) +
                      lit<")"> + (ref<NEWLINE> | eof)) |
                     (ref<LBRACK> + some(append<&ast::Node::body>(ref<Item>)) +
                      ref<RBRACK> + (ref<NEWLINE> | eof)) |
                     (ref<LBRACE> + some(append<&ast::Node::body>(ref<Item>)) +
                      ref<RBRACE> + (ref<NEWLINE> | eof)) |
                     (ref<LANGLE> + some(append<&ast::Node::body>(ref<Item>)) +
                      ref<RANGLE> + (ref<NEWLINE> | eof)) |
                     (ref<NEWLINE> | eof)))> {};
  struct Root : rule<create<ast::Node>(
                    many(append<&ast::Node::body>(ref<Stmt>)))> {};
  using entry = Root;
  using ignored = WS;
  using indent_newline = NEWLINE;
  static constexpr auto indent_open =
      lit<"("> | lit<"["> | ref<LBRACE> | ref<LANGLE>;
  static constexpr auto indent_close =
      lit<")"> | lit<"]"> | ref<RBRACE> | lit<">">;
};
struct Ll : Base {};
struct Lr : Base {};
}  // namespace tdelim
