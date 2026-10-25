#pragma once

// Two shapes whose stack grows with the document: parentheses, where only the
// outermost call names the member the innermost token fills, and blocks,
// where every level names its own.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tcaretdepth {

struct Expression : cursiv::Extends<cursiv::AstNode> {};
struct Name : cursiv::Extends<Expression> {
  string_view text;
};
struct Statement : cursiv::Extends<cursiv::AstNode> {};
struct Leaf : cursiv::Extends<Statement> {
  string_view text;
};
struct Scope : cursiv::Extends<Statement> {
  vector<Statement*> items;
};
struct Document : cursiv::Extends<cursiv::AstNode> {
  Expression* value{};
  Scope* body{};
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Atom : rule<create<Name>(assign<&Name::text>(ref<ID>))> {};
  struct Expr : rule<ref<Atom> | (lit<"("> + ref<Expr> + lit<")">)> {};
  struct Stmt;
  struct Block
      : rule<create<Scope>(lit<"{"> + many(append<&Scope::items>(ref<Stmt>)) +
                           lit<"}">)> {};
  struct Stmt : rule<ref<Block> |
                     create<Leaf>(assign<&Leaf::text>(ref<ID>) + lit<";">)> {};
  struct Root : rule<create<Document>(
                    (kw<"expr"> + assign<&Document::value>(ref<Expr>)) |
                    (kw<"block"> + assign<&Document::body>(ref<Block>)))> {};
  using entry = Root;
  using ignore = WS;
};

struct Ll : Base {};
struct Lr : Base {};

}  // namespace tcaretdepth
