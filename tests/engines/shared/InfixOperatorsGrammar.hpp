#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include <cstdint>
// Operators of a precedence table written in place as sequences: each reads as
// an unnamed data rule, and its value is what a data rule's would be.

namespace tinfixop {

enum class Op : std::uint8_t { equal, NOTEQUAL };
enum class Vis : std::uint8_t { Public, Protected, Private };

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
// Declares no member of its own: its base declares the three a climb writes.
struct Recoded : cursiv::Extends<Coded> {};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  Expr* value = nullptr;
};
struct Decl : cursiv::Extends<cursiv::AstNode> {
  Vis vis = Vis::Private;
  string_view name;
  bool seen = false;
};
}  // namespace ast

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct EQ : terminal<as<Op::equal>(lit<"=">)> {};
  using ignored = WS;
};

struct Joined : Base {
  struct E;
  struct Primary : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>)) |
                        (lit<"("> + ref<E> + lit<")">)> {};
  struct E : infix<Primary, ast::Text, (left(kw<"NOT"> + kw<"EQUAL">))> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::value>(ref<E>)) + eof> {
  };
};

struct Enumerated : Base {
  struct E;
  struct Primary : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>)) |
                        (lit<"("> + ref<E> + lit<")">)> {};
  struct E : infix<Primary, ast::Coded, (left(kw<"NOT"> + kw<"EQUAL">))> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::value>(ref<E>)) + eof> {
  };
};

struct Terminal : Base {
  struct E;
  struct Primary : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>)) |
                        (lit<"("> + ref<E> + lit<")">)> {};
  struct E : infix<Primary, ast::Coded, (left(ref<EQ>))> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::value>(ref<E>)) + eof> {
  };
};

struct Guarded : Base {
  struct E;
  struct Primary : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>)) |
                        (lit<"("> + ref<E> + lit<")">)> {};
  struct E : infix<Primary, ast::Coded, (left(ref<EQ> + &lit<"(">))> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::value>(ref<E>)) + eof> {
  };
};

struct Adjacent : Base {
  struct E;
  struct Primary : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>)) |
                        (lit<"("> + ref<E> + lit<")">)> {};
  struct E
      : infix<Primary, ast::Text, (left(lit<">"> + gap::none + lit<">">))> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::value>(ref<E>)) + eof> {
  };
};

struct Tight : Base {
  struct E;
  struct Primary : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>)) |
                        (lit<"("> + ref<E> + lit<")">)> {};
  struct E : infix<Primary, ast::Text, (left(tight(lit<">"> + lit<">">)))> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::value>(ref<E>)) + eof> {
  };
};

struct Shared : Base {
  struct E;
  struct Primary : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>)) |
                        (lit<"("> + ref<E> + lit<")">)> {};
  struct E : infix<Primary, ast::Text,
                   levels(left(kw<"IS"> + kw<"EQUAL">),
                          left(kw<"IS"> + kw<"LIKE">))> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::value>(ref<E>)) + eof> {
  };
};

struct Arm : Base {
  struct E;
  struct Primary : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>)) |
                        (lit<"("> + ref<E> + lit<")">)> {};
  struct E : infix<Primary, ast::Text,
                   (left(lit<"+"> | (kw<"NOT"> + kw<"EQUAL">)))> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::value>(ref<E>)) + eof> {
  };
};

struct Grouped : Base {
  struct E;
  struct Primary : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>)) |
                        (lit<"("> + ref<E> + lit<")">)> {};
  struct E : infix<Primary, ast::Text,
                   levels(left(kw<"A"> + kw<"B">), right(kw<"C"> + kw<"D">),
                          nonassoc(kw<"E"> + kw<"F">))> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::value>(ref<E>)) + eof> {
  };
};

struct Between : Base {
  struct E;
  struct Primary : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>)) |
                        (lit<"("> + ref<E> + lit<")">)> {};
  struct E : infix<Primary, ast::Text,
                   (left(kw<"NOT"> + !gap::line + kw<"EQUAL">))> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::value>(ref<E>)) + eof> {
  };
};

struct Lead : Base {
  struct E;
  struct Primary : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>)) |
                        (lit<"("> + ref<E> + lit<")">)> {};
  struct E : infix<Primary, ast::Coded, (left(!gap::line + ref<EQ>))> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::value>(ref<E>)) + eof> {
  };
};

// `as<V>` names in place the constant an operator denotes: one token is a
// terminal of its own, a phrase a value rule with trivia between its words.
struct Valued : Base {
  struct E;
  struct NE : terminal<lit<"<>">> {};
  struct Primary : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>)) |
                        (lit<"("> + ref<E> + lit<")">)> {};
  struct E : infix<Primary, ast::Coded,
                   left(as<Op::equal>(lit<"==">) |
                        as<Op::NOTEQUAL>(kw<"IS"> + kw<"NOT">) |
                        as<Op::NOTEQUAL>(ref<NE>) |
                        as<Op::NOTEQUAL>(ref<terminal<lit<"~=">>>))> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::value>(ref<E>)) + eof> {
  };
};

// `as<V>` as the payload of an assignment: the member takes the constant,
// whether one word or two spelled it.
struct Stated : Base {
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::value>(
                         create<ast::Coded>(assign<&ast::Coded::op>(
                             as<Op::equal>(kw<"SAME">) |
                             as<Op::NOTEQUAL>(kw<"NOT"> + kw<"SAME">))))) +
                     eof> {};
};

// A data rule passes the value of the alternative it took: the enumerator,
// whether the rule is called, called through another, or the alternative is
// written in place. A region holding the call and a condition, behind a
// condition of its own, passes it too: its sequence stays a group, which the
// descent records when a repair drops a token inside it. A condition that
// ends a region reads past the caller's trivia, as one after the region does,
// even trivia the region would refuse, whether it writes or a repetition
// holds it. A condition held by a repetition or a region of its own leaves
// the value to the call it follows. Two words may spell one value.
struct Visible : Base {
  struct NOTE : terminal<lit<"#">> {};
  struct Visibility : rule<as<Vis::Public>(kw<"public">) |
                           as<Vis::Protected>(kw<"protected">) |
                           as<Vis::Private>(kw<"private">)> {};
  struct Wordy : rule<as<Vis::Public>(kw<"is"> + kw<"public">) |
                      as<Vis::Protected>(kw<"is"> + kw<"protected">)> {};
  struct Access : rule<ref<Visibility>> {};
  struct Tight : rule<tight(ref<Visibility> + &ref<ID>)> {};
  struct Region : rule<skip(ignore<WS>, hide<>, ref<Visibility> + &ref<ID>) +
                       &ref<ID>> {};
  struct Root
      : rule<create<ast::Decl>(
                 (kw<"called"> + assign<&ast::Decl::vis>(ref<Visibility>)) |
                 (kw<"lone"> + assign<&ast::Decl::vis>(ref<Access>)) |
                 (kw<"wordy"> + assign<&ast::Decl::vis>(ref<Wordy>)) |
                 (kw<"region"> + assign<&ast::Decl::vis>(ref<Region>)) |
                 (kw<"tight"> + assign<&ast::Decl::vis>(ref<Tight>)) |
                 (kw<"regioned"> +
                  assign<&ast::Decl::vis>(
                      skip(ignore<WS>, hide<>, ref<Visibility> + &ref<ID>) +
                      &ref<ID>)) |
                 (kw<"tightened"> +
                  assign<&ast::Decl::vis>(tight(ref<Visibility> + &ref<ID>))) |
                 (kw<"tightenedrepeated"> +
                  assign<&ast::Decl::vis>(
                      tight(ref<Visibility> + repeat<1>(&ref<ID>)))) |
                 (kw<"regionedtail"> +
                  assign<&ast::Decl::vis>(ref<Visibility> +
                                          skip(ignore<WS>, hide<>, &ref<ID>))) |
                 (kw<"tightbefore"> +
                  assign<&ast::Decl::vis>(tight(ref<Visibility>) + &ref<ID>)) |
                 (kw<"flagged"> +
                  tight(assign<&ast::Decl::vis>(ref<Visibility>) +
                        flag<&ast::Decl::seen>(&ref<ID>))) |
                 (kw<"repeated"> +
                  tight(assign<&ast::Decl::vis>(ref<Visibility>) +
                        repeat<1>(&ref<ID>))) |
                 (kw<"flaggedrepeated"> +
                  tight(assign<&ast::Decl::vis>(ref<Visibility>) +
                        flag<&ast::Decl::seen>(repeat<1>(&ref<ID>)))) |
                 (kw<"inline"> +
                  assign<&ast::Decl::vis>(as<Vis::Public>(kw<"public">) |
                                          as<Vis::Protected>(kw<"protected">) |
                                          as<Vis::Private>(kw<"private">)))) +
             assign<&ast::Decl::name>(ref<ID>) + eof> {};
};

// `infix<>` builds the class it names, though a base declares the members.
struct Derived : Base {
  struct E;
  struct Primary : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>)) |
                        (lit<"("> + ref<E> + lit<")">)> {};
  struct E : infix<Primary, ast::Recoded,
                   levels(left(as<Op::NOTEQUAL>(lit<"!=">)),
                          right(as<Op::equal>(lit<":=">)))> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::value>(ref<E>)) + eof> {
  };
};

// An operator a recursive data rule spells: any number of `!`, then `+`.
struct Recursive : Base {
  struct E;
  struct Bang : rule<lit<"+"> | (lit<"!"> + ref<Bang>)> {};
  struct Primary : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>)) |
                        (lit<"("> + ref<E> + lit<")">)> {};
  struct E
      : infix<Primary, ast::Text, levels(left(ref<Bang>), left(lit<"-">))> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::value>(ref<E>)) + eof> {
  };
};

// A condition in an operator reads the token its first terminals lengthen,
// in front of the operator's token and behind it.
struct Lengthened : Base {
  struct E;
  struct Primary
      : rule<create<ast::Name>(many(lit<">">) +
                               assign<&ast::Name::name>(ref<ID>)) |
             (lit<"("> + ref<E> + lit<")">)> {};
  struct E : infix<Primary, ast::Text,
                   levels(left(lit<"+">), left(!lit<"->"> + lit<"-">),
                          left(lit<"*"> + !lit<">>">))> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::value>(ref<E>)) + eof> {
  };
};

// Two tables give `+` a level and a grouping each. A table whose operand ends
// with the other's expression leaves `+` to that expression.
struct Tables : Base {
  struct Sum;
  struct Type;
  struct Primary : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>)) |
                        (lit<"("> + ref<Sum> + lit<")">)> {};
  struct Sum
      : infix<Primary, ast::Text, levels(left(lit<"*">), left(lit<"+">))> {};
  struct Atom
      : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>)) |
             (lit<"["> + ref<Type> + lit<"]">) | (lit<"@"> + ref<Sum>)> {};
  struct Type
      : infix<Atom, ast::Text, levels(right(lit<"+">), left(lit<"|">))> {};
  struct Root : rule<create<ast::Doc>(
                         (lit<"sum:"> + assign<&ast::Doc::value>(ref<Sum>)) |
                         (lit<"type:"> + assign<&ast::Doc::value>(ref<Type>))) +
                     eof> {};
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
                     (lit<"lead:"> + ref<Lead::Root>) |
                     (lit<"valued:"> + ref<Valued::Root>) |
                     (lit<"stated:"> + ref<Stated::Root>) |
                     (lit<"visible:"> + ref<Visible::Root>) |
                     (lit<"noted:"> + skip(ignore<WS, Visible::NOTE>, hide<>,
                                           ref<Visible::Root>)) |
                     (lit<"derived:"> + ref<Derived::Root>) |
                     (lit<"recursive:"> + ref<Recursive::Root>) |
                     (lit<"lengthened:"> + ref<Lengthened::Root>) |
                     (lit<"tables:"> + ref<Tables::Root>)> {};
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
  struct E : infix<Primary, ast::Binary,
                   levels(left(lit<"*~">), left(lit<"+~">),
                          nonassoc(lit<"<~"> | lit<">~">), left(lit<"&~">))> {};
  struct Root
      : rule<create<ast::Document>(assign<&ast::Document::value>(ref<E>)) +
             eof> {};
  using entry = Root;
  using ignored = WS;
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
  using ignored = WS;
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
  struct E : infix<Primary, Binary, left(lit<"+">)> {};
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
  struct E : infix<Primary, Binary, levels(left(ref<Star>), left(lit<"+">))> {};
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
