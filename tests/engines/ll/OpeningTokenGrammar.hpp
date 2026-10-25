#pragma once

// Operators on different steps that open on one token and part on the text
// after it: `>>` written as two `>` against `>`, `->` against `-` and `>`,
// `not in` and `not like` against each other and against a prefix `not`.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace topening {

namespace ast {
struct Expression : cursiv::Extends<cursiv::AstNode> {};
struct Number : cursiv::Extends<Expression> {
  string_view text;
};
struct Binary : cursiv::Extends<Expression> {
  Expression* left{};
  string_view op;
  Expression* right{};
};
struct Negation : cursiv::Extends<Expression> {
  Expression* operand{};
};
struct Document : cursiv::Extends<cursiv::AstNode> {
  Expression* value{};
};
}  // namespace ast

struct Shift : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NUMBER : terminal<some(d)> {};
  struct ShiftOp : rule<(lit<"<"> + lit<"<">) | (lit<">"> + lit<">">)> {};
  struct ArrowOp : rule<lit<"-"> + lit<">">> {};
  struct Expr;
  struct Atom : rule<assign<&ast::Number::text>(ref<NUMBER>) |
                     (lit<"("> + ref<Expr> + lit<")">)> {};
  struct Expr
      : infix<Atom, &ast::Binary::left, &ast::Binary::op, &ast::Binary::right,
              (left(lit<"*">) >> left(lit<"+"> | lit<"-">) >>
               left(ref<ShiftOp>) >> left(lit<"<"> | lit<">">) >>
               right(ref<ArrowOp>))> {};
  struct Root : rule<assign<&ast::Document::value>(ref<Expr>) + eof> {};
  using entry = Root;
  using ignore = WS;
};
struct ShiftLl : Shift {};
struct ShiftLr : Shift {};

struct Negated : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NUMBER : terminal<some(d)> {};
  struct NotIn : rule<kw<"not"> + kw<"in">> {};
  struct NotLike : rule<kw<"not"> + kw<"like">> {};
  struct Expr;
  struct Unary
      : rule<create<ast::Negation>(
                 kw<"not"> + assign<&ast::Negation::operand>(ref<Unary>))  //
             | assign<&ast::Number::text>(ref<NUMBER>)                     //
             | (lit<"("> + ref<Expr> + lit<")">)> {};
  struct Expr
      : infix<Unary, &ast::Binary::left, &ast::Binary::op, &ast::Binary::right,
              (left(lit<"+">) >> left(kw<"like"> | ref<NotLike>) >>
               left(kw<"in"> | ref<NotIn>) >> left(kw<"and">))> {};
  struct Root : rule<assign<&ast::Document::value>(ref<Expr>) + eof> {};
  using entry = Root;
  using ignore = WS;
};
struct NegatedLl : Negated {};
struct NegatedLr : Negated {};

// Two tables, the inner one tighter: the shift of the outer table opens on the
// `>` the inner table's comparison is.
struct Tables : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NUMBER : terminal<some(d)> {};
  struct ShiftOp : rule<lit<">"> + lit<">">> {};
  struct Atom : rule<assign<&ast::Number::text>(ref<NUMBER>)> {};
  struct Compare : infix<Atom, &ast::Binary::left, &ast::Binary::op,
                         &ast::Binary::right, left(lit<">"> | lit<"<">)> {};
  struct Expr
      : infix<Compare, &ast::Binary::left, &ast::Binary::op,
              &ast::Binary::right, (left(ref<ShiftOp>) >> left(lit<"?">))> {};
  struct Root : rule<assign<&ast::Document::value>(ref<Expr>) + eof> {};
  using entry = Root;
  using ignore = WS;
};
struct TablesLl : Tables {};
struct TablesLr : Tables {};

// Operators a rule says whole: one behind a condition, one that is a choice,
// one of two tokens behind the same `*` as a token of its own.
struct Composite : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NUMBER : terminal<some(d)> {};
  struct SameLine : rule<!gap::line + lit<"+">> {};
  struct Words : rule<lit<"-"> | (lit<"a"> + lit<"b">)> {};
  struct Starred : rule<lit<"*"> + (lit<"x"> | lit<"y">)> {};
  struct Atom : rule<assign<&ast::Number::text>(ref<NUMBER>)> {};
  struct Expr
      : infix<Atom, &ast::Binary::left, &ast::Binary::op, &ast::Binary::right,
              (left(ref<Starred>) >> left(lit<"*">) >> left(ref<SameLine>) >>
               left(ref<Words>))> {};
  struct Root : rule<assign<&ast::Document::value>(ref<Expr>) + eof> {};
  using entry = Root;
  using ignore = WS;
};
struct CompositeLl : Composite {};
struct CompositeLr : Composite {};

}  // namespace topening
