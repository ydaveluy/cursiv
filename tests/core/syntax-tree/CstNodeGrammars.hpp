#pragma once

// The constructions of the DSL on the smallest grammars that spell them. Three
// lexical cases share a grammar, each body entered by its own first token; a
// table whose cells a lane pins keeps a grammar of its own.

#include <string_view>

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tcstn {

namespace ast {
struct Node : cursiv::Extends<cursiv::AstNode> {};
}  // namespace ast

// `ikw` reads one word in either case, and the boundary still refuses a longer
// one; a block comment runs to its literal closer; `repeat<4>` counts the
// escape json and json5 spell; and `@` refuses the digit behind it, which an
// identifier may begin with, so only that frontier can say no.
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
  std::string_view op;
  Expr* right = nullptr;
  std::string_view value;
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

// Two single-byte terminals that overlap without being equal: `*` is both a
// class of its own and a member of the class `UN` reads. Only the state
// can choose -- where it admits one, the reserve takes it; where it admits
// both, the reading forks and the branch that cannot continue dies. STAR is a
// class: a literal would outrank UN at `*` and leave nothing to fork.
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
// blanks are spelled twice -- run, then refused -- because a token graph keeps
// every length of a run open, and the run must be maximal for the refusal to
// stand where the author put it.
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
