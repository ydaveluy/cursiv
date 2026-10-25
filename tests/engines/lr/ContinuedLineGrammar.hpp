#pragma once

// A line continued by a backslash starts no logical line. Two derivations of
// one text, the first with an empty reduction before the token past the
// continuation: read ahead from a cell, the first stands as the election
// orders it.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tcontinued {
namespace ast {
struct Node : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  vector<Node*> body;
};
struct Less : cursiv::Extends<Node> {};
struct More : cursiv::Extends<Node> {};
}  // namespace ast

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t)">) | (lit<"\\"> + eol)> {};
  struct NEWLINE : terminal<eol + many(many(cr<R"( \t)">) + eol)> {};
  struct INDENT : terminal<indent> {};
  struct DEDENT : terminal<dedent> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct P : rule<some(ref<ID>)> {};
  struct Q : rule<some(ref<ID>)> {};
  struct Mark : rule<option(lit<"!">)> {};
  struct Less : rule<create<ast::Less>(
                    assign<&ast::Node::name>(ref<P>) + lit<";"> +
                    ref<Mark> + lit<"<"> + lit<"1"> + ref<NEWLINE>)> {};
  struct More : rule<create<ast::More>(
                    assign<&ast::Node::name>(ref<Q>) + lit<";"> +
                    lit<"<"> + lit<"1"> + ref<NEWLINE>)> {};
  struct Root : rule<create<ast::Node>(
                    kw<"s"> + lit<":"> + ref<NEWLINE> + ref<INDENT> +
                    some(append<&ast::Node::body>(ref<Less> | ref<More>)) +
                    ref<DEDENT>)> {};
  using entry = Root;
  using ignore = WS;
};
struct Ll : G {};
struct Lr : G {};
}  // namespace tcontinued
