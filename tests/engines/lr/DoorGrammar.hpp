#pragma once

// Small languages the lanes read through the parser, each behind its own
// opening keyword in one grammar: no rule is shared, so each case reads as
// its grammar alone does -- a comment is kept in the tree wherever one is
// read.

#include <cstdint>
#include <string_view>

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tdoor {
struct Words : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct COMMENT : terminal<through(lit<"/*">, lit<"*/">)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct NAME : terminal<cr<"a-zA-Z_"> + many(w)> {};
  struct DIGITS : terminal<some(d)> {};
  struct INT : terminal<as<std::int64_t>(some(d))> {};
};
}  // namespace tdoor

// A precedence table whose operators are data rules: several tokens, trivia
// between them, and a converter that hands `op` a value.
namespace tinfix {

enum class Cmp : std::uint8_t { is, is_not, less, leq, both };

// A data rule hands over its parts joined, so the trivia between the words is
// not in the text the converter reads.
inline constexpr auto cmp_of = [](std::string_view t) {
  if (t == "is") return Cmp::is;
  if (t == "isless") return Cmp::less;
  return Cmp::leq;
};

namespace ast {
struct Expr : cursiv::Extends<cursiv::AstNode> {};
struct Bin : cursiv::Extends<Expr> {
  Expr* left = nullptr;
  Cmp op = Cmp::is;
  Expr* right = nullptr;
};
struct Name : cursiv::Extends<Expr> {
  string_view name;
};
}  // namespace ast

struct G : tdoor::Words {
  struct CmpOp : rule<as<cmp_of>(
                     (kw<"is"> + kw<"less"> + option(kw<"or"> + kw<"equal">)) |
                     kw<"is">)> {};
  // One operator, two spellings, one constant: the text is never read.
  struct NotEq : rule<as<Cmp::is_not>((kw<"is"> + kw<"not">) | lit<"!=">)> {};
  struct AndOp : rule<as<[] { return Cmp::both; }>(kw<"and">)> {};
  struct Primary : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>))> {
  };
  struct Expr : infix<Primary, ast::Bin,
                      levels(left(ref<CmpOp> | ref<NotEq>), left(ref<AndOp>))> {
  };
};

}  // namespace tinfix

// Data rules whose parts a repair can insert: a sign and its digits, a
// version of two numbers, a pair handed to a parts converter.
namespace tdatains {

// The fewest parts the converter was ever handed.
inline std::size_t fewestParts = 3;
inline constexpr auto pair_parts = [](cursiv::Parts p) -> std::int64_t {
  if (p.size() < fewestParts) fewestParts = p.size();
  return p.size() < 3 ? -7 : std::int64_t(p[0].size() * 100 + p[2].size());
};

namespace ast {
struct Decl : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  int64 value = -99;
  float64 ver = -1;
  int64 pair = -1;
};
struct Model : cursiv::Extends<cursiv::AstNode> {
  vector<Decl*> decls;
};
}  // namespace ast

struct G : tdoor::Words {
  struct Neg : rule<option(lit<"-">) + ref<DIGITS>> {};
  struct Ver : rule<ref<DIGITS> + lit<"."> + ref<DIGITS>> {};
  struct PairR : rule<as<pair_parts>(ref<DIGITS> + lit<":"> + ref<DIGITS>)> {};
  struct Decl
      : rule<create<ast::Decl>(
            kw<"let"> + assign<&ast::Decl::name>(ref<ID>) + lit<"="> +
            assign<&ast::Decl::value>(ref<Neg>) +
            option(lit<"v"> + assign<&ast::Decl::ver>(ref<Ver>)) +
            option(lit<"@"> + assign<&ast::Decl::pair>(ref<PairR>)) +
            lit<";">)> {};
  struct Model
      : rule<create<ast::Model>(many(append<&ast::Model::decls>(ref<Decl>)))> {
  };
};

}  // namespace tdatains

// Postfix nests under an infix: a repair that gives up on part of a postfix
// chain leaves its finished nests inside the recovered region, with no rule
// of theirs left to wrap.
namespace tnestrec {

namespace ast {
struct Expr : cursiv::Extends<cursiv::AstNode> {};
struct Name : cursiv::Extends<Expr> {
  string_view name;
};
struct Num : cursiv::Extends<Expr> {
  int64 value = 0;
};
struct Bin : cursiv::Extends<Expr> {
  Expr* left = nullptr;
  string_view op;
  Expr* right = nullptr;
};
struct Call : cursiv::Extends<Expr> {
  Expr* callee = nullptr;
  vector<Expr*> args;
};
struct Member : cursiv::Extends<Expr> {
  Expr* receiver = nullptr;
  string_view member;
};
struct Stmt : cursiv::Extends<cursiv::AstNode> {
  Expr* expr = nullptr;
};
struct Model : cursiv::Extends<cursiv::AstNode> {
  vector<Stmt*> stmts;
};
}  // namespace ast

struct G : tdoor::Words {
  struct Expr;
  struct NameR : rule<assign<&ast::Name::name>(ref<ID>)> {};
  struct NumR : rule<assign<&ast::Num::value>(ref<INT>)> {};
  struct Paren : rule<lit<"("> + ref<Expr> + lit<")">> {};
  struct Postfix
      : rule<(ref<NameR> | ref<NumR> | ref<Paren>) +
             many(nest<ast::Call, &ast::Call::callee>(
                      lit<"("> +
                      many(append<&ast::Call::args>(ref<Expr>), lit<",">) +
                      lit<")">) |
                  nest<ast::Member, &ast::Member::receiver>(
                      lit<"."> + assign<&ast::Member::member>(ref<ID>)))> {};
  struct Sum
      : infix<Postfix, ast::Bin, levels(left(lit<"*">), left(lit<"+">))> {};
  struct Expr : rule<ref<Sum>> {};
  struct StmtR : rule<assign<&ast::Stmt::expr>(ref<Expr>) + lit<";">> {};
  struct Model
      : rule<create<ast::Model>(many(append<&ast::Model::stmts>(ref<StmtR>)))> {};
};

}  // namespace tnestrec

// A character class holding SOME bytes past 0x7F and not all: the eight-byte
// step reads a lane's low seven bits, so what it makes of the high half is
// all or nothing.
namespace thigh {

namespace ast {
struct Doc : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
}  // namespace ast

struct G : tdoor::Words {
  struct HIGH : terminal<some(cr<R"(^A-Z?=)">)> {};
  // Spellings the generated source must still compile: a trigraph, and
  // bytes that start no scalar.
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::name>(ref<HIGH>) +
                                      option(lit<"?\?="> | lit<"\xff\xfe">))> {};
};

}  // namespace thigh

// One byte, and a long spelling of the same byte: at every position the walk
// runs to the end of the run before falling back to the short match.
namespace tmunch {

namespace ast {
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> parts;
};
}  // namespace ast

struct G : tdoor::Words {
  struct A : terminal<lit<"a">> {};
  // An unbounded run and then a byte the run never holds: the walk reaches
  // the end of every run before it learns there is no `b`.
  struct LONG : terminal<some(cr<"a">) + lit<"b">> {};
  struct Root : rule<create<ast::Doc>(
                    some(append<&ast::Doc::parts>(ref<A> | ref<LONG>)))> {};
};

}  // namespace tmunch

// A shift/reduce cell on a NAMED terminal whose entry bytes are its own: the
// state's fast path probes those bytes, and an open cell may not be bypassed.
namespace tnamedconf {

namespace ast {
struct A : cursiv::Extends<cursiv::AstNode> {
  string_view x;
  string_view y;
};
struct S : cursiv::Extends<cursiv::AstNode> {
  A* a = nullptr;
  string_view z;
  string_view w;
};
}  // namespace ast

struct G : tdoor::Words {
  struct ARule
      : rule<create<ast::A>(assign<&ast::A::x>(ref<ID>)) |
             create<ast::A>(assign<&ast::A::x>(ref<ID>) +
                            assign<&ast::A::y>(ref<ID>))> {};
  // Two more names after the group: at one `ID` the cell must choose, and
  // for three names in all it is the REDUCE that reaches the end.
  struct SRule : rule<create<ast::S>(assign<&ast::S::a>(ref<ARule>) +
                                     assign<&ast::S::z>(ref<ID>) +
                                     assign<&ast::S::w>(ref<ID>))> {};
  using entry = SRule;
  using ignored = WS;
};

}  // namespace tnamedconf

// A precedence table and an adjacency island in one grammar, with no path from
// the island to the table; then the same island over a rule that writes.
namespace tinfr {

namespace ast {
struct Item : cursiv::Extends<cursiv::AstNode> {};
struct Num : cursiv::Extends<Item> {
  string_view text;
};
struct Bin : cursiv::Extends<Item> {
  Item* left = nullptr;
  string_view op;
  Item* right = nullptr;
};
struct Key : cursiv::Extends<Item> {
  string_view head;
  vector<string_view> tail;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Item*> items;
};
}  // namespace ast

struct G : tdoor::Words {
  struct Primary
      : rule<create<ast::Num>(assign<&ast::Num::text>(ref<DIGITS>))> {};
  struct Sum
      : infix<Primary, ast::Bin, levels(left(lit<"*">), left(lit<"+">))> {};

  // The island reaches a rule, so the second reading is live here.
  struct Dotted : rule<ref<NAME> + many(lit<"."> + ref<NAME>)> {};
  struct Island
      : rule<create<ast::Key>(tight(lit<"["> + ref<Dotted> + lit<"]">))> {};

  struct Line : rule<ref<Island> | ref<Sum>> {};
  struct Root
      : rule<create<ast::Doc>(some(append<&ast::Doc::items>(ref<Line>)))> {};
  using entry = Root;
  using ignored = WS;
};

// The island adopts the node its rule built and hands it up through `Line`,
// which spans one symbol and lays no record: the append at the top names
// `Line`, so the record it rides is owed by whoever spans the text.
namespace wri {

struct G : tdoor::Words {
  struct Dotted : rule<create<ast::Key>(
                      assign<&ast::Key::head>(ref<NAME>) +
                      many(lit<"."> + append<&ast::Key::tail>(ref<NAME>)))> {};
  struct Island
      : rule<tight(lit<"["> + ref<Dotted> + lit<"]">)> {};
  struct Line : rule<ref<Island>> {};
  struct Root
      : rule<create<ast::Doc>(some(append<&ast::Doc::items>(ref<Line>)))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace wri

}  // namespace tinfr

// An infix rule read with no operator hands back its operand, not a node of
// its own class; a rule that adopts it and may nest over it closes on what
// both can be.

namespace tnestinfix {

namespace ast {
struct Expr : cursiv::Extends<cursiv::AstNode> {};
struct Ident : cursiv::Extends<Expr> {
  string_view name;
};
struct IntLit : cursiv::Extends<Expr> {
  int64 value;
};
struct Assign : cursiv::Extends<Expr> {
  Ident* target;
  Expr* value;
};
struct Bin : cursiv::Extends<Expr> {
  Expr* left;
  string_view op;
  Expr* right;
};
struct Stmt : cursiv::Extends<cursiv::AstNode> {
  Expr* value;
};
}  // namespace ast

struct Stmts : tdoor::Words {
  struct Expr;
  struct Identifier : rule<assign<&ast::Ident::name>(ref<ID>)> {};
  struct IntegerLiteral : rule<assign<&ast::IntLit::value>(ref<INT>)> {};
  // A parenthesis closes the cycle back through `Expr`, as in tinyc.
  struct ParenExpr : rule<lit<"("> + ref<Expr> + lit<")">> {};
  struct Term : rule<ref<Identifier> | ref<IntegerLiteral> | ref<ParenExpr>> {};
  static constexpr auto OpAdd = lit<"+">;
  struct Sum : infix<Term, ast::Bin, left(OpAdd)> {};
  struct Test : rule<ref<Sum> +
                     option(nest<ast::Bin, &ast::Bin::left>(
                         assign<&ast::Bin::op>(lit<"<">) +
                         assign<&ast::Bin::right>(ref<Sum>)))> {};
  struct Expr : rule<create<ast::Assign>(
                    assign<&ast::Assign::target>(ref<Identifier>) +
                    lit<"="> + assign<&ast::Assign::value>(ref<Expr>)) |
                ref<Test>> {};
  struct Stmt : rule<option(assign<&ast::Stmt::value>(ref<Expr>)) +
                     lit<";">> {};
};

// The parenthesis reaches the infix through a pass-through rule that
// commits nothing: the alternative answers nothing on that path, and the
// named alternative carries the type.
namespace paren {
namespace ast {
struct Ty : cursiv::Extends<cursiv::AstNode> {};
struct Named : cursiv::Extends<Ty> {
  string_view name;
};
struct Bin : cursiv::Extends<Ty> {
  Ty* left;
  string_view op;
  Ty* right;
};
struct Decl : cursiv::Extends<cursiv::AstNode> {
  Ty* type;
};
}  // namespace ast
struct G : tdoor::Words {
  struct Type_;
  struct Primary : rule<create<ast::Named>(assign<&ast::Named::name>(ref<ID>)) |
                        (lit<"("> + ref<Type_> + lit<")">)> {};
  struct BinT : infix<Primary, ast::Bin, left(lit<"|">)> {};
  struct Type_ : rule<ref<BinT>> {};
  struct Root : rule<create<ast::Decl>(kw<"let"> + lit<":"> +
                                       assign<&ast::Decl::type>(ref<Type_>))> {};
};
}  // namespace paren

// The two in one rule: a declaration opens on its keyword.
struct G : tdoor::Words {
  struct Doc : rule<ref<Stmts::Stmt> | ref<paren::G::Root>> {};
};

}  // namespace tnestinfix

// Two alternatives that part on their last token alone, with and without a
// repetition standing between the shared prefix and that token.

namespace ttwin {

namespace ast {
struct Node : cursiv::Extends<cursiv::AstNode> {};
struct A : cursiv::Extends<Node> {
  std::string_view name;
};
struct B : cursiv::Extends<Node> {
  std::string_view name;
};
struct Body : cursiv::Extends<Node> {
  cursiv::vector<std::string_view> items;
};
struct RA : cursiv::Extends<Node> {
  cursiv::vector<std::string_view> items;
  Body* body = nullptr;
};
struct RB : cursiv::Extends<Node> {
  cursiv::vector<std::string_view> items;
  Body* body = nullptr;
};
}  // namespace ast

// Bare twins: nothing stands between the shared prefix and the parting token.
struct Base : tdoor::Words {
  struct ARule : rule<create<ast::A>(kw<"k"> + assign<&ast::A::name>(ref<NAME>) +
                                     kw<"end">)> {};
  struct BRule : rule<create<ast::B>(kw<"k"> + assign<&ast::B::name>(ref<NAME>) +
                                     kw<"done">)> {};
  struct Root : rule<ref<ARule> | ref<BRule>> {};
};

// The same twins with a repetition between the prefix and the parting token.
struct RepBase : tdoor::Words {
  struct ARule
      : rule<create<ast::RA>(kw<"k"> + lit<"{"> +
                             many(append<&ast::RA::items>(ref<NAME>), lit<";">) +
                             lit<"}"> + kw<"end">)> {};
  struct BRule
      : rule<create<ast::RB>(kw<"k"> + lit<"{"> +
                             many(append<&ast::RB::items>(ref<NAME>), lit<";">) +
                             lit<"}"> + kw<"done">)> {};
  struct Root : rule<ref<ARule> | ref<BRule>> {};
};

// The same twins, the repetition factored into one shared rule.
struct SharedBase : tdoor::Words {
  struct Body
      : rule<create<ast::Body>(
            lit<"{"> + many(append<&ast::Body::items>(ref<NAME>), lit<";">) +
            lit<"}">)> {};
  struct ARule : rule<create<ast::RA>(
                     kw<"k"> + assign<&ast::RA::body>(ref<Body>) + kw<"end">)> {
  };
  struct BRule
      : rule<create<ast::RB>(kw<"k"> + assign<&ast::RB::body>(ref<Body>) +
                             kw<"done">)> {};
  struct Root : rule<ref<ARule> | ref<BRule>> {};
};

}  // namespace ttwin

namespace tdoor {
struct G : Words {
  struct Case : rule<(kw<"infix"> + ref<tinfix::G::Expr>) |
                     (kw<"data"> + ref<tdatains::G::Model>) |
                     (kw<"nest"> + ref<tnestrec::G::Model>) |
                     (kw<"high"> + ref<thigh::G::Root>) |
                     (kw<"munch"> + ref<tmunch::G::Root>) |
                     (kw<"named"> + ref<tnamedconf::G::SRule>) |
                     (kw<"region"> + ref<tinfr::G::Root>) |
                     (kw<"wri"> + ref<tinfr::wri::G::Root>) |
                     (kw<"after"> + ref<tnestinfix::G::Doc>) |
                     (kw<"plain"> + ref<ttwin::Base::Root>) |
                     (kw<"rep"> + ref<ttwin::RepBase::Root>) |
                     (kw<"sh"> + ref<ttwin::SharedBase::Root>)> {};
  using entry = Case;
  using ignored = WS;
  using hidden = list<COMMENT>;
};
}  // namespace tdoor
