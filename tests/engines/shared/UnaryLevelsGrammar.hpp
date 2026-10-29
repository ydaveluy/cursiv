#pragma once

#include <cstdint>

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

// Prefix and postfix levels in a precedence table: each level reads the one
// below it as its operand, so a prefix applies to everything that binds
// tighter than it, and an operand never starts with a looser prefix.

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
                          prefix<&ast::Not::word, &ast::Not::body>(kw<"NOT">),
                          left(kw<"AND">), left(kw<"OR">))> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::value>(ref<E>)) + eof> {
  };
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

// A table its primary reaches at its left edge: every level is a rule.
struct Cyclic : Base {
  struct E;
  struct Primary : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>)) |
                        (lit<"("> + ref<E> + lit<")">) |
                        create<ast::Wrapped>(
                            assign<&ast::Wrapped::value>(ref<E>) + lit<"?">)> {
  };
  struct E : infix<Primary, ast::Binary,
                   levels(postfix<ast::Postfix>(as<Step::inc>(lit<"++">)),
                          prefix<ast::Signed>(lit<"-">), left(lit<"*">))> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::value>(ref<E>)) + eof> {
  };
};

struct Suite : Base {
  struct Root : rule<(lit<"unary:"> + ref<Table::Root>) |
                     (lit<"members:"> + ref<Members::Root>) |
                     (lit<"cyclic:"> + ref<Cyclic::Root>)> {};
  using entry = Root;
};
struct Ll : Suite {};
struct Lr : Suite {};

// The member table alone, for a fuzz lane: its language is part of the
// suite's, so the suite's parsers read what it writes.
struct Fuzzed : Base {
  struct Root : rule<lit<"members:"> + ref<Members::Root>> {};
  using entry = Root;
};

}  // namespace tunary
