#pragma once

// Statements whose first token is told apart by a condition that reads two
// whole terminals past it, the shape a ported grammar's guards take, in the
// two engines a caret is asked of.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tcaretphrase {

struct Expr : cursiv::Extends<cursiv::AstNode> {};
struct Num : cursiv::Extends<Expr> {
  string_view text;
};
struct Name : cursiv::Extends<Expr> {
  string_view id;
};
struct Binary : cursiv::Extends<Expr> {
  Expr* left{};
  string_view op;
  Expr* right{};
};
struct Stmt : cursiv::Extends<cursiv::AstNode> {};
struct Let : cursiv::Extends<Stmt> {
  string_view name;
  Expr* value{};
};
struct Eval : cursiv::Extends<Stmt> {
  Expr* value{};
};
struct Each : cursiv::Extends<Stmt> {
  string_view var;
  Expr* range{};
  vector<Stmt*> body;
};
struct Unit : cursiv::Extends<cursiv::AstNode> {
  vector<Stmt*> stmts;
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct COMMENT : terminal<lit<"//"> + many(cr<R"(^\r\n)">)> {};
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};
  struct NUM : terminal<some(d)> {};

  struct Expression;
  struct Primary : rule<create<Num>(assign<&Num::text>(ref<NUM>)) |
                        create<Name>(assign<&Name::id>(ref<ID>)) |
                        (lit<"("> + ref<Expression> + lit<")">)> {};
  struct Expression
      : infix<Primary, &Binary::left, &Binary::op, &Binary::right,
              (left(lit<"*"> | lit<"/">) >> left(lit<"+"> | lit<"-">))> {};

  struct Statement;
  struct LetStmt
      : rule<create<Let>(kw<"let"> + assign<&Let::name>(ref<ID>) + lit<"="> +
                         assign<&Let::value>(ref<Expression>) + lit<";">)> {};
  struct EvalStmt
      : rule<create<Eval>(
            !(ref<ID> + kw<"in">)+assign<&Eval::value>(ref<Expression>) +
            lit<";">)> {};
  struct EachStmt : rule<create<Each>(
                        kw<"for"> + assign<&Each::var>(ref<ID>) + kw<"in"> +
                        assign<&Each::range>(ref<Expression>) + lit<"{"> +
                        many(append<&Each::body>(ref<Statement>)) + lit<"}">)> {
  };
  struct Statement : rule<ref<LetStmt> | ref<EachStmt> | ref<EvalStmt>> {};

  struct UnitRule
      : rule<create<Unit>(many(append<&Unit::stmts>(ref<Statement>)))> {};

  using entry = UnitRule;
  using ignore = WS;
  using hidden = COMMENT;
};

struct Ll : Base {};
struct Lr : Base {};

}  // namespace tcaretphrase
