#pragma once

// Statements a semicolon or a line break ends, as ECMAScript reads them: a
// refused token behind a line break, a closing brace, or the end of input
// implies the semicolon; a token the statement can take does not, and a
// restricted production refuses a line break before what follows it.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tasi {

namespace ast {
struct Stmt : cursiv::Extends<cursiv::AstNode> {
  string_view head;
  vector<Stmt*> args;
  vector<Stmt*> body;
  bool ret = false;
  bool empty = false;
  bool inc = false;
  bool go = false;
  vector<Stmt*> divs;
  bool obj = false;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Stmt*> body;
};
}  // namespace ast

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct COMMENT : terminal<through(lit<"/*">, lit<"*/">)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  // Two readings of `/`: an operator after an operand, a regex where an
  // operand is owed -- after `return`, never after a name.
  struct REGEX : terminal<lit<"/"> + some(cr<"a-z ">) + lit<"/">> {};
  static constexpr auto eos =
      implied(lit<";">, gap::line | &lit<"}"> | eof);
  struct Expr;
  // A name, called any number of times: `a \n (b)` is one call. A postfix
  // `++` may not follow a line break: `a \n ++` ends the statement at `a`.
  struct Expr : rule<create<ast::Stmt>(
                    (assign<&ast::Stmt::head>(ref<ID> | ref<REGEX>) |
                     (flag<&ast::Stmt::obj>(lit<"{">) +
                      assign<&ast::Stmt::head>(ref<ID>) + lit<"}">)) +
                    many(lit<"("> +
                         many(append<&ast::Stmt::args>(ref<Expr>), lit<",">) +
                         lit<")">) +
                    many(lit<"/"> + append<&ast::Stmt::divs>(ref<Expr>)) +
                    option(!gap::line + flag<&ast::Stmt::inc>(lit<"++">)))> {};
  struct Stmt;
  struct Block : rule<create<ast::Stmt>(
                     lit<"{"> + many(append<&ast::Stmt::body>(ref<Stmt>)) +
                     lit<"}">)> {};
  // `return \n x` is a bare return, then `x`: the operand may not follow a
  // line break.
  struct Return : rule<create<ast::Stmt>(
                      flag<&ast::Stmt::ret>(kw<"return">) +
                      option(!gap::line + append<&ast::Stmt::args>(ref<Expr>)) +
                      eos)> {};
  struct Empty : rule<create<ast::Stmt>(flag<&ast::Stmt::empty>(lit<";">))> {};
  struct ExprStmt : rule<!lit<"{"> + ref<Expr> + eos> {};
  // `go` is a keyword and a name both, as `yield` is: `go c` hands over,
  // `go \n c` is a bare `go` then `c`, and `go(c)` a call of the name.
  struct Go : rule<create<ast::Stmt>(
                  flag<&ast::Stmt::go>(kw<"go">) +
                  option(!gap::line + append<&ast::Stmt::args>(ref<Expr>)) + eos)> {};
  struct Stmt : rule<ref<ExprStmt> | ref<Block> | ref<Return> | ref<Go> |
                     ref<Empty>> {};
  struct Doc : rule<create<ast::Doc>(many(append<&ast::Doc::body>(ref<Stmt>)))> {};
  using entry = Doc;
  using ignore = list<WS, COMMENT>;
};

// The lexicon the small grammars below share, so that one grammar can hold
// them all.
struct Words : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct COMMENT : terminal<through(lit<"/*">, lit<"*/">)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
};

// Operators the gaps around them classify, as Swift reads them: spaced on
// both sides or on neither is binary, spaced before only is prefix, spaced
// after only is postfix.
namespace ops {
namespace ast {
struct Node : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  string_view op;
  vector<Node*> kids;
};
struct Pre : cursiv::Extends<Node> {};
struct Post : cursiv::Extends<Node> {};
struct Bin : cursiv::Extends<Node> {};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Node*> items;
};
}  // namespace ast
struct G : Words {
  struct OP : terminal<cr<"-+">> {};
  struct Atom : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>))> {};
  struct Unary;
  struct Unary
      : rule<create<ast::Pre>(assign<&ast::Node::op>(ref<OP>) + gap::none +
                              append<&ast::Node::kids>(ref<Unary>)) |
             create<ast::Post>(append<&ast::Node::kids>(ref<Atom>) +
                               gap::none + assign<&ast::Node::op>(ref<OP>) +
                               !gap::none) |
             ref<Atom>> {};
  struct Expr;
  struct Expr
      : rule<create<ast::Bin>(append<&ast::Node::kids>(ref<Unary>) +
                              gap::none + assign<&ast::Node::op>(ref<OP>) +
                              gap::none + append<&ast::Node::kids>(ref<Expr>)) |
             create<ast::Bin>(append<&ast::Node::kids>(ref<Unary>) +
                              !gap::none + assign<&ast::Node::op>(ref<OP>) +
                              !gap::none + append<&ast::Node::kids>(ref<Expr>)) |
             ref<Unary>> {};
  struct Doc
      : rule<create<ast::Doc>(many(append<&ast::Doc::items>(ref<Expr>)))> {};
  using entry = Doc;
  using ignore = list<WS, COMMENT>;
};
}  // namespace ops

// The same operators with the gap after them closing a choice: that
// condition is asked when the alternative reduces, on the token after it.
namespace tail {
namespace ast {
struct Node : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  string_view op;
  vector<Node*> kids;
};
struct Bin : cursiv::Extends<Node> {};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Node*> items;
};
}  // namespace ast
struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct OP : terminal<cr<"-+">> {};
  struct Atom : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>))> {};
  struct BinaryOp
      : rule<create<ast::Bin>(
            ((gap::none + assign<&ast::Node::op>(ref<OP>) + gap::none) |
             (!gap::none + assign<&ast::Node::op>(ref<OP>) + !gap::none)) +
            append<&ast::Node::kids>(ref<Atom>))> {};
  struct Expr
      : rule<create<ast::Node>(append<&ast::Node::kids>(ref<Atom>) +
                               many(append<&ast::Node::kids>(ref<BinaryOp>)))> {
  };
  struct Doc
      : rule<create<ast::Doc>(
            many(append<&ast::Doc::items>(ref<Expr>) + lit<";">))> {};
  using entry = Doc;
  using ignore = WS;
};
}  // namespace tail

// A statement that opens on `{` is a block; the statements that could also
// open there refuse the brace, and the one inside ends on an implied `;`.
namespace block {
namespace ast {
struct Node : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  vector<Node*> body;
};
struct Block : cursiv::Extends<Node> {};
struct Var : cursiv::Extends<Node> {};
struct Obj : cursiv::Extends<Node> {};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Node*> items;
};
}  // namespace ast
struct G : Words {
  static constexpr auto Eos = implied(lit<";">, gap::line | &lit<"}"> | eof);
  struct Name : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>))> {};
  struct Obj
      : rule<create<ast::Obj>(lit<"{"> +
                              option(append<&ast::Node::body>(ref<Name>)) +
                              lit<"}">)> {};
  struct Primary : rule<ref<Name> | ref<Obj>> {};
  struct Declarator
      : rule<create<ast::Var>(append<&ast::Node::body>(ref<Obj>) + lit<"="> +
                              append<&ast::Node::body>(ref<Primary>))> {};
  struct Stmt;
  struct Block
      : rule<create<ast::Block>(lit<"{"> +
                                many(append<&ast::Node::body>(ref<Stmt>)) +
                                lit<"}">)> {};
  struct VarStmt
      : rule<create<ast::Node>(!lit<"{"> +
                               append<&ast::Node::body>(ref<Declarator>)) +
             Eos> {};
  struct ExprStmt
      : rule<create<ast::Node>(!lit<"{"> +
                               append<&ast::Node::body>(ref<Primary>) + Eos)> {};
  struct Stmt : rule<ref<Block> | ref<VarStmt> | ref<ExprStmt>> {};
  struct Doc
      : rule<create<ast::Doc>(many(append<&ast::Doc::items>(ref<Stmt>)))> {};
  using entry = Doc;
  using ignore = WS;
};
}  // namespace block

// A condition before an optional position: a line break refuses the
// statement, and the empty option must not be read past it.
namespace optgap {
namespace ast {
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> names;
};
}  // namespace ast
struct G : Words {
  struct Bang : rule<option(lit<"!">)> {};
  struct Doc
      : rule<create<ast::Doc>(many(kw<"go"> + !gap::line + ref<Bang> +
                                   append<&ast::Doc::names>(ref<ID>) +
                                   lit<";">))> {};
  using entry = Doc;
  using ignore = WS;
};

// The empty option before a literal: the gap decides where its goto lands,
// on the literal as on any token.
namespace close {
struct G : Words {
  struct Bang : rule<option(lit<"!">)> {};
  struct Doc
      : rule<create<ast::Doc>(many(append<&ast::Doc::names>(ref<ID>) +
                                   !gap::line + ref<Bang> + lit<";">))> {};
  using entry = Doc;
  using ignore = WS;
};
}  // namespace close
}  // namespace optgap

// Conditions and implied tokens past the semicolon: a named token, two owed
// at one position, a keyword read with its boundary and its case, and
// conditions composed in a rule.
namespace more {
struct G : Words {
  struct SEMI : terminal<lit<";">> {};
  // `loop \n x` owes a `:` then a `;`, both at `x`.
  struct Loop : rule<kw<"loop"> + implied(lit<":">, gap::line) +
                     implied(ref<SEMI>, gap::line | eof)> {};
  // `use a in b`: `in` opens the source, `inner` is a second name on the
  // same line.
  struct Use : rule<kw<"use"> + ref<ID> +
                    option(!kw<"in"> + !gap::line + ref<ID>) +
                    option(kw<"in"> + ref<ID>) +
                    implied(lit<";">, gap::line | &lit<"}"> | eof)> {};
  // Names each ended by a `.`, implied before `END` in any case.
  struct Blk : rule<kw<"begin"> +
                    many(ref<ID> + implied(lit<".">, gap::line | &ikw<"end">)) +
                    ikw<"end">> {};
  // An operand on the same line only, and a call whose `(` does not open a
  // line.
  struct Tail : rule<kw<"tail"> + option(!(gap::line | eof) + ref<ID>) +
                     lit<";">> {};
  struct Call : rule<kw<"call"> +
                     option(!(gap::line + &lit<"(">) + lit<"("> + lit<")">) +
                     lit<";">> {};
  // A condition in front of an optional tail, then the implied `;`: a
  // refused guard leaves the implied token in hand for the repair.
  struct Bang : rule<kw<"bang"> + ref<ID> + option(gap::none + lit<"!">) +
                     implied(lit<";">, gap::line | eof)> {};
  struct Stmt : rule<ref<Loop> | ref<Use> | ref<Blk> | ref<Tail> | ref<Call> |
                     ref<Bang>> {};
  struct Doc : rule<create<ast::Doc>(many(ref<Stmt>))> {};
  using entry = Doc;
  using ignore = WS;
};
}  // namespace more

// The insertion asked by a class: it reads exactly the token's bytes, so
// `&cr<"}">` holds on the byte of the literal `}` the grammar spells.
namespace brace {
struct G : Words {
  struct Stmt;
  struct Block : rule<create<ast::Stmt>(
                     lit<"{"> + many(append<&ast::Stmt::body>(ref<Stmt>)) +
                     lit<"}">)> {};
  struct Name : rule<create<ast::Stmt>(
                    assign<&ast::Stmt::head>(ref<ID>) +
                    implied(lit<";">, &cr<"}"> | eof))> {};
  struct Stmt : rule<ref<Block> | ref<Name>> {};
  struct Doc : rule<create<ast::Doc>(many(append<&ast::Doc::body>(ref<Stmt>)))> {};
  using entry = Doc;
  using ignore = WS;
};
}  // namespace brace

// What no token wide closes ends where the last token did: an implied `;`,
// an empty tail. The comment behind it belongs to what follows.
namespace doc {
struct G : Words {
  struct NUM : terminal<some(cr<"0-9">)> {};
  struct Tail : rule<create<ast::Stmt>(
                    option(assign<&ast::Stmt::head>(ref<NUM>)))> {};
  struct Simple : rule<create<ast::Stmt>(
                      assign<&ast::Stmt::head>(ref<ID>) +
                      implied(lit<";">, gap::line | eof))> {};
  struct Item : rule<create<ast::Stmt>(
                    lit<"@"> + assign<&ast::Stmt::head>(ref<ID>) +
                    append<&ast::Stmt::body>(ref<Tail>))> {};
  struct Stmt : rule<ref<Simple> | ref<Item>> {};
  struct Doc : rule<create<ast::Doc>(many(append<&ast::Doc::body>(ref<Stmt>)))> {};
  using entry = Doc;
  using ignore = WS;
  using hidden = list<COMMENT>;
};
}  // namespace doc

// A precedence table whose operators open on a condition: the step binds the
// token past it, so the table still decides.
namespace prec {
namespace ast {
struct Expr : cursiv::Extends<cursiv::AstNode> {};
struct Bin : cursiv::Extends<Expr> {
  Expr* left = nullptr;
  string_view op;
  Expr* right = nullptr;
};
struct Name : cursiv::Extends<Expr> {
  string_view name;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Expr*> items;
};
}  // namespace ast
struct G : Words {
  struct Plus : rule<!gap::line + lit<"+">> {};
  struct Star : rule<!gap::line + lit<"*">> {};
  struct Primary : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>))> {
  };
  struct Expr : infix<Primary, &ast::Bin::left, &ast::Bin::op, &ast::Bin::right,
                      (left(ref<Star>) >> left(ref<Plus>))> {};
  struct Doc
      : rule<create<ast::Doc>(many(append<&ast::Doc::items>(ref<Expr>)))> {};
  using entry = Doc;
  using ignore = WS;
};
}  // namespace prec

// One rule behind a line break or not: the goto that closes it lands by what
// held where it opened.
namespace held {
struct G : Words {
  struct Prefix : rule<lit<"a"> + lit<"b">> {};
  struct Root : rule<create<ast::Doc>((!gap::line + ref<Prefix> + lit<"x">) |
                                      (gap::line + ref<Prefix> + lit<"y">))> {};
  using entry = Root;
  using ignore = WS;
};
}  // namespace held

// A closing tag that opens on an operator: where the implied `;` may stand,
// `?>` lexes whole for the condition to read, and is never taken as `?`.
namespace tag {
struct G : Words {
  struct Expr;
  struct Expr : rule<create<ast::Stmt>(
                    assign<&ast::Stmt::head>(ref<ID>) +
                    option(lit<"?"> + append<&ast::Stmt::args>(ref<Expr>) +
                           lit<":"> + append<&ast::Stmt::args>(ref<Expr>)))> {};
  struct Stmt : rule<ref<Expr> + implied(lit<";">, &lit<"?>">)> {};
  struct Doc : rule<create<ast::Doc>(
                   lit<"<?"> + many(append<&ast::Doc::body>(ref<Stmt>)) +
                   lit<"?>">)> {};
  using entry = Doc;
  using ignore = WS;
};
struct WithLine : G {
  struct Close : terminal<lit<"?>"> + option(eol)> {};
  struct Stmt : rule<ref<Expr> + implied(lit<";">, &ref<Close>)> {};
  struct Doc : rule<create<ast::Doc>(lit<"<?"> +
                                     many(append<&ast::Doc::body>(ref<Stmt>)) +
                                     ref<Close>)> {};
  using entry = Doc;
};
}  // namespace tag

}  // namespace tasi

namespace tasi::gaps {
struct G : Words {
  struct Root
      : rule<create<tasi::ast::Doc>(
            (lit<"a"> + gap::none + lit<";">) |
            (lit<"b"> + gap::has(eol | lit<"\u2028"> | lit<"\u2029">) +
             lit<";">) |
            (lit<"c"> + gap::has(cr<"a-c"> | lit<"ab"> | lit<"xa">) +
             lit<";">) |
            (lit<"d"> + gap::is(lit<"ab"> | lit<"cd">) + lit<";">) |
            (lit<"e"> + gap::has(some(cr<"x">) + cr<"yz">) + lit<";">) |
            (lit<"f"> + gap::is(option(cr<"a">)) + lit<";">) |
            (lit<"h"> +
             gap::has(repeat<2, 4>(cr<"x">) + (lit<"ab"> | lit<"yz">)) +
             lit<";">) |
            (lit<"i"> + gap::is(many(option(cr<"a">)) + option(cr<"b">)) +
             lit<";">) |
            (lit<"j"> + gap::has(lit<"x", !cr<"α">>) + lit<";">) |
            (lit<"g"> + !(gap::line | &lit<";">)+lit<";">))> {};
  using entry = Root;
  using ignore = WS;
};
}  // namespace tasi::gaps

// The small grammars above in one, each behind its own opening keyword: no
// rule is shared, so each case reads as its grammar alone does -- a comment
// is kept in the tree wherever one is read.
namespace tasi::cases {
struct G : Words {
  struct Case
      : rule<(kw<"ops"> + ref<ops::G::Doc>) |
             (kw<"block"> + ref<block::G::Doc>) |
             (kw<"optgap"> + ref<optgap::G::Doc>) |
             (kw<"close"> + ref<optgap::close::G::Doc>) |
             (kw<"more"> + ref<more::G::Doc>) |
             (kw<"brace"> + ref<brace::G::Doc>) |
             (kw<"doc"> + ref<doc::G::Doc>) |
             (kw<"prec"> + ref<prec::G::Doc>) |
             (kw<"held"> + ref<held::G::Root>) |
             (kw<"tag"> + ref<tag::G::Doc>) |
             (kw<"namedtag"> + ref<tag::WithLine::Doc>) |
             (kw<"tailops"> + ref<tail::G::Doc>) |
             (kw<"gaps"> + ref<gaps::G::Root>)> {};
  using entry = Case;
  using ignore = WS;
  using hidden = list<COMMENT>;
};
}  // namespace tasi::cases
