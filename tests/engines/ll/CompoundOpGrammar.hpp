#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tllcompound {

enum class Op { add, sub, implies, comma, not_in, not_like };
struct Expression : cursiv::Extends<cursiv::AstNode> {};
struct Number : cursiv::Extends<Expression> {
  string_view text;
};
struct Binary : cursiv::Extends<Expression> {
  Expression* left{};
  Op op{};
  Expression* right{};
};
struct Document : cursiv::Extends<cursiv::AstNode> {
  Expression* value{};
};
struct Pair : cursiv::Extends<cursiv::AstNode> {
  string_view key;
  Expression* value{};
};
struct Object : cursiv::Extends<Expression> {
  vector<Pair*> pairs;
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct COMMENT : terminal<through(lit<"/*">, lit<"*/">)> {};
  struct NUMBER : terminal<some(d)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Expr;
  struct ObjectRule;
  struct Atom : rule<assign<&Number::text>(ref<NUMBER>) | ref<ObjectRule> |
                     (lit<"("> + ref<Expr> + lit<")">)> {};
  struct PairRule : rule<assign<&Pair::key>(ref<ID>) + lit<":"> +
                         assign<&Pair::value>(ref<Expr>)> {};
  struct ObjectRule
      : rule<create<Object>(
            lit<"{"> +
            option(some(append<&Object::pairs>(ref<PairRule>), lit<",">)) +
            lit<"}">)> {};
  struct Add : terminal<lit<"+">, value<Op::add>> {};
  struct Sub : terminal<lit<"-">, value<Op::sub>> {};
  struct Implies : terminal<lit<"->">, value<Op::implies>> {};
  struct Comma : terminal<lit<",">, value<Op::comma>> {};
  struct NotIn : rule<!gap::line + kw<"not"> + kw<"in">, value<Op::not_in>> {};
  struct NotLike
      : rule<!gap::line + kw<"not"> + kw<"like">, value<Op::not_like>> {};
  struct Inner : infix<Atom, &Binary::left, &Binary::op, &Binary::right,
                       left(ref<Add> | ref<Sub>)> {};
  struct Expr : infix<Inner, &Binary::left, &Binary::op, &Binary::right,
                      (left(ref<NotIn> | ref<NotLike>) >> right(ref<Implies>) >>
                       left(ref<Comma>))> {};
  struct Root : rule<assign<&Document::value>(ref<Expr>) + eof> {};
  using entry = Root;
  using ignore = WS;
  using hidden = COMMENT;
};

// An operator written as a rule, alone on its first token: no prediction
// chooses it.
struct Plain : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NUMBER : terminal<some(d)> {};
  // Distinct spellings allow different precedence levels in the shared table.
  struct Add : terminal<lit<"++">, value<Op::add>> {};
  struct NotIn : rule<kw<"not"> + kw<"within">, value<Op::not_in>> {};
  struct Atom : rule<assign<&Number::text>(ref<NUMBER>)> {};
  struct Expr : infix<Atom, &Binary::left, &Binary::op, &Binary::right,
                      (left(ref<Add>) >> left(ref<NotIn>))> {};
  struct Root : rule<assign<&Document::value>(ref<Expr>) + eof> {};
  using entry = Root;
  using ignore = WS;
};

// Preserve each mode's lexer policy while sharing generated parsers and AST
// metadata: comments are hidden only in the compound-operator mode.
struct Suite : cursiv::GrammarSpec {
  struct Root
      : rule<(lit<"compound:"> +
              skip(ignored<Base::WS>, hide<Base::COMMENT>, ref<Base::Root>)) |
             (lit<"plain:"> +
              skip(ignored<Plain::WS>, hide<>, ref<Plain::Root>))> {};
  using entry = Root;
  using ignore = Base::WS;
  using hidden = Base::COMMENT;
};
struct Ll : Suite {};
struct Lr : Suite {};

}  // namespace tllcompound
