#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include <cstdint>
// Operators of a precedence table written in place as sequences: each reads as
// an unnamed data rule, and its value is what a data rule's would be.

namespace tinfixop {

enum class Op : std::uint8_t { equal, NOTEQUAL };

namespace ast {
struct Expr : cursiv::Extends<cursiv::AstNode> {};
struct Name : cursiv::Extends<Expr> {
  string_view name;
};
struct Text : cursiv::Extends<Expr> {
  Expr* left = nullptr;
  string_view op;
  Expr* right = nullptr;
};
struct Coded : cursiv::Extends<Expr> {
  Expr* left = nullptr;
  Op op = Op::equal;
  Expr* right = nullptr;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  Expr* value = nullptr;
};
}  // namespace ast

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct EQ : terminal<lit<"=">, value<Op::equal>> {};
  using ignore = WS;
};

struct Joined : Base {
  struct E;
  struct Primary : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>)) |
                        (lit<"("> + ref<E> + lit<")">)> {};
  struct E : infix<Primary, &ast::Text::left, &ast::Text::op, &ast::Text::right,
                   (left(kw<"NOT"> + kw<"EQUAL">))> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::value>(ref<E>)) + eof> {
  };
};

struct Enumerated : Base {
  struct E;
  struct Primary : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>)) |
                        (lit<"("> + ref<E> + lit<")">)> {};
  struct E : infix<Primary, &ast::Coded::left, &ast::Coded::op,
                   &ast::Coded::right, (left(kw<"NOT"> + kw<"EQUAL">))> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::value>(ref<E>)) + eof> {
  };
};

struct Terminal : Base {
  struct E;
  struct Primary : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>)) |
                        (lit<"("> + ref<E> + lit<")">)> {};
  struct E : infix<Primary, &ast::Coded::left, &ast::Coded::op,
                   &ast::Coded::right, (left(ref<EQ>))> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::value>(ref<E>)) + eof> {
  };
};

struct Guarded : Base {
  struct E;
  struct Primary : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>)) |
                        (lit<"("> + ref<E> + lit<")">)> {};
  struct E : infix<Primary, &ast::Coded::left, &ast::Coded::op,
                   &ast::Coded::right, (left(ref<EQ> + &lit<"(">))> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::value>(ref<E>)) + eof> {
  };
};

struct Adjacent : Base {
  struct E;
  struct Primary : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>)) |
                        (lit<"("> + ref<E> + lit<")">)> {};
  struct E : infix<Primary, &ast::Text::left, &ast::Text::op, &ast::Text::right,
                   (left(lit<">"> + gap::none + lit<">">))> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::value>(ref<E>)) + eof> {
  };
};

struct Tight : Base {
  struct E;
  struct Primary : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>)) |
                        (lit<"("> + ref<E> + lit<")">)> {};
  struct E : infix<Primary, &ast::Text::left, &ast::Text::op, &ast::Text::right,
                   (left(tight(lit<">"> + lit<">">)))> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::value>(ref<E>)) + eof> {
  };
};

struct Shared : Base {
  struct E;
  struct Primary : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>)) |
                        (lit<"("> + ref<E> + lit<")">)> {};
  struct E
      : infix<Primary, &ast::Text::left, &ast::Text::op, &ast::Text::right,
              (left(kw<"IS"> + kw<"EQUAL">) >> left(kw<"IS"> + kw<"LIKE">))> {
  };
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::value>(ref<E>)) + eof> {
  };
};

struct Arm : Base {
  struct E;
  struct Primary : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>)) |
                        (lit<"("> + ref<E> + lit<")">)> {};
  struct E : infix<Primary, &ast::Text::left, &ast::Text::op, &ast::Text::right,
                   (left(lit<"+"> | (kw<"NOT"> + kw<"EQUAL">)))> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::value>(ref<E>)) + eof> {
  };
};

struct Grouped : Base {
  struct E;
  struct Primary : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>)) |
                        (lit<"("> + ref<E> + lit<")">)> {};
  struct E : infix<Primary, &ast::Text::left, &ast::Text::op, &ast::Text::right,
                   (left(kw<"A"> + kw<"B">) >> right(kw<"C"> + kw<"D">) >>
                    nonassoc(kw<"E"> + kw<"F">))> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::value>(ref<E>)) + eof> {
  };
};

struct Between : Base {
  struct E;
  struct Primary : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>)) |
                        (lit<"("> + ref<E> + lit<")">)> {};
  struct E : infix<Primary, &ast::Text::left, &ast::Text::op, &ast::Text::right,
                   (left(kw<"NOT"> + !gap::line + kw<"EQUAL">))> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::value>(ref<E>)) + eof> {
  };
};

struct Lead : Base {
  struct E;
  struct Primary : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>)) |
                        (lit<"("> + ref<E> + lit<")">)> {};
  struct E : infix<Primary, &ast::Coded::left, &ast::Coded::op,
                   &ast::Coded::right, (left(!gap::line + ref<EQ>))> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::value>(ref<E>)) + eof> {
  };
};


struct Suite : Base {
  struct Root : rule<(lit<"joined:"> + ref<Joined::Root>) |
                     (lit<"enumerated:"> + ref<Enumerated::Root>) |
                     (lit<"terminal:"> + ref<Terminal::Root>) |
                     (lit<"guarded:"> + ref<Guarded::Root>) |
                     (lit<"adjacent:"> + ref<Adjacent::Root>) |
                     (lit<"tight:"> + ref<Tight::Root>) |
                     (lit<"arm:"> + ref<Arm::Root>) |
                     (lit<"grouped:"> + ref<Grouped::Root>) |
                     (lit<"between:"> + ref<Between::Root>) |
                     (lit<"lead:"> + ref<Lead::Root>)> {};
  using entry = Root;
};

}  // namespace tinfixop

// A level that groups nothing: two of its operators in a row, with nothing
// that groups between them, are no expression.

namespace tnonassoc {

namespace ast {
struct Expr : cursiv::Extends<cursiv::AstNode> {};
struct Atom : cursiv::Extends<Expr> {
  string_view name;
};
struct Binary : cursiv::Extends<Expr> {
  Expr* left = nullptr;
  string_view op;
  Expr* right = nullptr;
};
struct Document : cursiv::Extends<cursiv::AstNode> {
  Expr* value = nullptr;
};
}  // namespace ast

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct E;
  struct Primary : rule<create<ast::Atom>(assign<&ast::Atom::name>(ref<ID>)) |
                        (lit<"("> + ref<E> + lit<")">)> {};
  struct E : infix<Primary, &ast::Binary::left, &ast::Binary::op,
                   &ast::Binary::right,
                   (left(lit<"*~">) >> left(lit<"+~">) >>
                    nonassoc(lit<"<~"> | lit<">~">) >> left(lit<"&~">))> {};
  struct Root
      : rule<create<ast::Document>(assign<&ast::Document::value>(ref<E>)) +
             eof> {};
  using entry = Root;
  using ignore = WS;
};


}  // namespace tnonassoc

namespace telectionloop {

struct Expression : cursiv::Extends<cursiv::AstNode> {};
struct Atom : cursiv::Extends<Expression> {};
struct Binary : cursiv::Extends<Expression> {
  Expression* left{};
  string_view op;
  Expression* right{};
};
struct Function : cursiv::Extends<Expression> {
  Expression* body{};
};
struct Include : cursiv::Extends<Expression> {
  Expression* body{};
};
struct Cast : cursiv::Extends<Expression> {
  Expression* body{};
};
struct Rc : cursiv::Extends<Expression> {
  bool extra{};
};
struct Ns : cursiv::Extends<Expression> {
  Rc* child{};
};
struct ExpressionDocument : cursiv::Extends<cursiv::AstNode> {
  Expression* value{};
};
struct Statement : cursiv::Extends<cursiv::AstNode> {};
struct Leaf : cursiv::Extends<Statement> {};
struct If : cursiv::Extends<Statement> {
  Statement* then{};
  Statement* other{};
};
struct StatementDocument : cursiv::Extends<cursiv::AstNode> {
  vector<Statement*> items;
};

struct Common : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  using ignore = WS;
};

struct Choice : Common {
  struct RC : rule<create<Rc>((lit<"x"> + flag<&Rc::extra>(lit<"y">)) |
                              lit<"x">)> {};
  struct RS
      : rule<create<Ns>(assign<&Ns::child>(ref<RC>) + lit<"y">) | ref<RC>> {};
  struct Root : rule<create<ExpressionDocument>(
                         assign<&ExpressionDocument::value>(ref<RS>)) +
                     eof> {};
  using entry = Root;
};

// One level of `infix` groups to the left. Its prefix primaries call the
// complete expression, so a function/include/cast body can still grow.
struct Direct : Common {
  struct E;
  struct Primary
      : rule<create<Atom>(lit<"a">) |
             create<Function>(kw<"fn"> + lit<"=>"> +
                              assign<&Function::body>(ref<E>)) |
             create<Include>(kw<"include"> + assign<&Include::body>(ref<E>)) |
             create<Cast>(kw<"cast"> + assign<&Cast::body>(ref<E>)) |
             (lit<"("> + ref<E> + lit<")">)> {};
  struct E : infix<Primary, &Binary::left, &Binary::op, &Binary::right,
                   left(lit<"+">)> {};
  struct Root : rule<create<ExpressionDocument>(
                         assign<&ExpressionDocument::value>(ref<E>)) +
                     eof> {};
  using entry = Root;
};

// Levels are declared tightest first. A named terminal and grouping exercise
// the same boundaries as inline operators and primaries.
struct Multi : Common {
  struct E;
  struct Star : terminal<lit<"*">> {};
  struct Primary
      : rule<create<Atom>(lit<"a">) |
             create<Function>(kw<"fn"> + lit<"=>"> +
                              assign<&Function::body>(ref<E>)) |
             create<Include>(kw<"include"> + assign<&Include::body>(ref<E>)) |
             create<Cast>(kw<"cast"> + assign<&Cast::body>(ref<E>)) |
             (lit<"("> + ref<E> + lit<")">)> {};
  struct E : infix<Primary, &Binary::left, &Binary::op, &Binary::right,
                   (left(ref<Star>) >> left(lit<"+">))> {};
  struct Root : rule<create<ExpressionDocument>(
                         assign<&ExpressionDocument::value>(ref<E>)) +
                     eof> {};
  using entry = Root;
};

struct Statements : Common {
  struct S;
  struct IfRule
      : rule<create<If>(kw<"if"> + kw<"c"> + kw<"then"> +
                        assign<&If::then>(ref<S>) +
                        option(kw<"else"> + assign<&If::other>(ref<S>)))> {};
  struct S : rule<ref<IfRule> | create<Leaf>(lit<"x">)> {};
  struct Root : rule<create<StatementDocument>(
                         append<&StatementDocument::items>(ref<S>)) +
                     eof> {};
  using entry = Root;
};
struct StatementLoop : Statements {
  struct Root : rule<create<StatementDocument>(
                         some(append<&StatementDocument::items>(ref<S>))) +
                     eof> {};
  using entry = Root;
};

struct Suite : Common {
  struct Root : rule<ref<Choice::Root> |
                     (lit<"direct:"> + ref<Direct::Root>) |
                     (lit<"multi:"> + ref<Multi::Root>) |
                     (lit<"statement:"> + ref<Statements::Root>) |
                     (lit<"loop:"> + ref<StatementLoop::Root>)> {};
  using entry = Root;
};
}  // namespace telectionloop

// All operator contracts have the same whitespace and word-boundary policy.
// Prefixes choose the original entries while sharing one pair of parsers.
namespace toperators {
struct Suite : tinfixop::Base {
  struct Root : rule<ref<tinfixop::Suite::Root> |
                     (lit<"shared:"> + ref<tinfixop::Shared::Root>) |
                     (lit<"nonassoc:"> + ref<tnonassoc::Base::Root>) |
                     (lit<"election:"> + ref<telectionloop::Suite::Root>)> {};
  using entry = Root;
};
struct Ll : Suite {};
struct Lr : Suite {};
}  // namespace toperators
