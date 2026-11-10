#pragma once

// The constructions of the DSL on the smallest grammars that spell them. Three
// lexical cases share a grammar, each body entered by its own first token; a
// table whose cells a test checks has a grammar of its own.

#include <string_view>

#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"

namespace tcstn {

namespace ast {
struct Node : cursiv::Extends<cursiv::AstNode> {};
}  // namespace ast

// `ikw` reads a word in either case, and the boundary still refuses a
// longer one; a block comment runs to its closer; `repeat<4>` counts an
// escape; `@` refuses a digit behind it.
struct LexBase : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t\r\n)">)> {};
  struct ML : terminal<through(lit<"/*">, lit<"*/">)> {};
  struct ID : terminal<some(cr<"a-z0-9">)> {};
  struct HEX : terminal<lit<"\\u"> + repeat<4>(cr<"0-9a-f">)> {};
  struct AT : terminal<lit<"@", !d>> {};
  struct Doc : rule<create<ast::Node>((ikw<"entity"> + ref<ID>) |
                                      some(ref<ID>, lit<",">) |
                                      some(ref<HEX>) |
                                      some(ref<AT> + ref<ID>, lit<",">))> {};
  using entry = Doc;
  using ignored = list<WS, ML>;
};

// Infix: the precedence table, lowered to one nonterminal; after `~`, the same
// table with one step declared to group right.
namespace ast {
struct Expr : cursiv::AstNode {
  Expr* left = nullptr;
  string_view op;
  Expr* right = nullptr;
  string_view value;
};
}  // namespace ast

struct InfixG : cursiv::GrammarSpec {
  struct Expr;
  struct WS : terminal<some(cr<R"( \t\n)">)> {};
  struct NUM : terminal<some(d)> {};
  struct Primary : rule<create<ast::Expr>(assign<&ast::Expr::value>(ref<NUM>)) |
                        (lit<"("> + ref<Expr> + lit<")">)> {};
  struct Expr
      : infix<Primary, ast::Expr,
              levels(left(lit<"*"> | lit<"/">), left(lit<"+"> | lit<"-">))> {};
  struct RightPrimary
      : rule<create<ast::Expr>(assign<&ast::Expr::value>(ref<NUM>))> {};
  struct RightExpr : infix<RightPrimary, ast::Expr, right(lit<"^">)> {};
  struct Root : rule<ref<Expr> | (lit<"~"> + ref<RightExpr>)> {};
  using entry = Root;
  using ignored = WS;
};

// Two single-byte terminals that overlap: `*` is STAR and also in the class
// `UN` reads. Where the state admits both, the parse forks.
struct OverlapG : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t\n)">)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct NUM : terminal<some(d)> {};
  struct STAR : terminal<cr<"*">> {};
  struct UN : terminal<cr<"!*~">> {};
  struct Mul : rule<ref<ID> + ref<STAR> + ref<ID>> {};
  struct App : rule<ref<ID> + ref<UN> + ref<NUM>> {};
  struct Doc : rule<create<ast::Node>(ref<Mul> | ref<App>)> {};
  using entry = Doc;
  using ignored = WS;
};

// A refused word inside a token: C's `#` line that is not `#include`. The
// blanks are spelled twice, so the run is maximal before the refusal.
struct WordGuardG : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t\r\n)">)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct DIRECTIVE
      : terminal<lit<"#"> + many(cr<R"( \t)">) + !cr<R"( \t)"> + !kw<"include"> +
                 many(cr<R"(^\r\n)">) + !cr<R"(^\r\n)">> {};
  struct Doc : rule<create<ast::Node>(many(ref<ID>))> {};
  using entry = Doc;
  using ignored = WS;
  using hidden = list<DIRECTIVE>;
};

struct LexG : LexBase {
  struct Entry : rule<ref<LexBase::Doc> |
                      (lit<"overlap:"> + ref<OverlapG::Doc>) |
                      (lit<"guard:"> + ref<WordGuardG::Doc>)> {};
  using entry = Entry;
  using hidden = list<WordGuardG::DIRECTIVE>;
};

}  // namespace tcstn
