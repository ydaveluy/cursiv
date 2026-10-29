#pragma once

// Shapes a pass of the construction once lost a sentence of, cells whose arms
// a rank or a phase elects, and words a state reserves or not: each behind its
// own opening keyword in one grammar. No rule is shared, so each case keeps
// the shape it pins.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "engines/lr/EmptyEntryGrammar.hpp"
#include "engines/lr/LongRunGrammar.hpp"

namespace tpasses {
struct Words : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};
};
}  // namespace tpasses

// Two auxiliaries of one shape under one rule, `(X | Y)` and `(Y | X)`: states
// that differ only by which of the two they goto on must not merge.
namespace tmin {

namespace ast {
struct Kid : cursiv::Extends<cursiv::AstNode> {};
struct XK : cursiv::Extends<Kid> {
  string_view t;
};
struct YK : cursiv::Extends<Kid> {
  string_view t;
};
struct Node : cursiv::Extends<cursiv::AstNode> {
  Kid* kid = nullptr;
};
}  // namespace ast

struct G : tpasses::Words {
  struct XT : terminal<lit<"x">> {};
  struct YT : terminal<lit<"y">> {};
  struct X : rule<create<ast::XK>(assign<&ast::XK::t>(ref<XT>))> {};
  struct Y : rule<create<ast::YK>(assign<&ast::YK::t>(ref<YT>))> {};
  struct R
      : rule<create<ast::Node>(
            (lit<"b"> + lit<"k"> + assign<&ast::Node::kid>(ref<Y> | ref<X>)) |
            (lit<"b"> + assign<&ast::Node::kid>(ref<Y> | ref<X>)) |
            (lit<"a"> + assign<&ast::Node::kid>(ref<X> | ref<Y>)) |
            (lit<"c"> + assign<&ast::Node::kid>(ref<Y> | ref<X>)) |
            (lit<"d"> + lit<"k"> + assign<&ast::Node::kid>(ref<X> | ref<Y>)) |
            (lit<"d"> + assign<&ast::Node::kid>(ref<X> | ref<Y>)))> {};
};

}  // namespace tmin

// An end-of-input assertion inside a unit alternative: the passes that skip a
// unit reduce must keep the restriction to `$end`.
namespace tend {

namespace ast {
struct Node : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
}  // namespace ast

struct G : tpasses::Words {
  struct Doc : rule<create<ast::Node>(
                   (lit<"!"> + assign<&ast::Node::name>(ref<ID>) +
                    ((lit<"#"> + eof) | lit<"@">)+option(lit<";">)) |
                   (lit<"("> + assign<&ast::Node::name>(ref<ID>) +
                    (lit<"*"> | lit<"&">)+lit<"="> + lit<"="> + lit<"$">) |
                   (lit<")"> + assign<&ast::Node::name>(ref<ID>) +
                    (lit<"*"> | lit<"&">)+lit<"=="> + lit<"$">))> {};
};

}  // namespace tend

// A conflict cell the second token decides, where one arm's reduce lands in a
// state of another lex mode: the drive re-reads there, so no peek decides it.
namespace tpeek {

namespace ast {
struct Top : cursiv::Extends<cursiv::AstNode> {};
}  // namespace ast

struct G : tpasses::Words {
  struct IN : terminal<kw<"in">> {};
  struct C : rule<lit<"x">> {};
  struct D : rule<lit<"x"> + ref<IN> + ref<ID> + ref<ID> + ref<ID>> {};
  struct TopR : rule<create<ast::Top>(
                    (lit<"a"> + ((ref<C> + ref<IN> + lit<"z">) | ref<D>)) |
                    (lit<"b"> + ((ref<C> + ref<ID> + ref<ID>) | ref<D>)))> {};
};

}  // namespace tpeek

// An unbounded repetition with a floor above one: the floor is the language's,
// not only a bounded repetition's.
namespace trepeat {

namespace ast {
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> names;
};
}  // namespace ast

struct G : tpasses::Words {
  struct Root : rule<create<ast::Doc>(repeat<2, cursiv::kUnbounded>(
                    append<&ast::Doc::names>(ref<ID>)))> {};
};

}  // namespace trepeat

// One alternative twenty positions wide: a production's positions are a run of
// the slot pool, so nothing caps how wide a rule may be spelled.
namespace twidealt {

namespace ast {
struct Row : cursiv::Extends<cursiv::AstNode> {
  string_view head;
  string_view tail;
};
}  // namespace ast

struct G : tpasses::Words {
  struct Root : rule<create<ast::Row>(
                    assign<&ast::Row::head>(ref<ID>) + lit<"+"> + lit<"-"> +
                    lit<"*"> + lit<"/"> + lit<"%"> + lit<"^"> + lit<"&"> +
                    lit<"|"> + lit<"~"> + lit<"<"> + lit<">"> + lit<"="> +
                    lit<":"> + lit<";"> + lit<","> + lit<"."> + lit<"?"> +
                    lit<"@"> + assign<&ast::Row::tail>(ref<ID>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace twidealt

// Two reductions of one text, ranked by the choice that holds them where the
// cell's terminal may follow both: `x c` is T1's, which writes A first, and
// `y c z` is T2's, which writes B first -- however wide either rule grows.
namespace trank {

namespace ast {
struct Kid : cursiv::Extends<cursiv::AstNode> {};
struct AKid : cursiv::Extends<Kid> {
  string_view t;
};
struct BKid : cursiv::Extends<Kid> {
  string_view t;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  Kid* kid = nullptr;
};
}  // namespace ast

struct Words : tpasses::Words {
  struct C : terminal<lit<"c">> {};
};

// One set of rules per grammar below: a rule two of them shared would merge
// their cells.
template <int>
struct Base : Words {
  struct A : rule<create<ast::AKid>(assign<&ast::AKid::t>(ref<C>))> {};
  struct B : rule<create<ast::BKid>(assign<&ast::BKid::t>(ref<C>))> {};
  struct T2 : rule<create<ast::Doc>(
                  (assign<&ast::Doc::kid>(ref<B>) + lit<"z">) |
                  (assign<&ast::Doc::kid>(ref<A>) + lit<"z">))> {};
};

namespace plain {
struct G : Base<0> {
  struct T1 : rule<create<ast::Doc>(assign<&ast::Doc::kid>(ref<A>) |
                                    assign<&ast::Doc::kid>(ref<B>))> {};
  struct S : rule<(lit<"x"> + ref<T1>) | (lit<"y"> + ref<T2>)> {};
};
}  // namespace plain

// An alternative of T1 that reads neither widens its closure past T2's.
namespace grown {
struct G : Base<1> {
  struct D : rule<lit<"d">> {};
  struct T1 : rule<create<ast::Doc>(assign<&ast::Doc::kid>(ref<A>) |
                                    assign<&ast::Doc::kid>(ref<B>) |
                                    (ref<D> + lit<"e">))> {};
  struct S : rule<(lit<"x"> + ref<T1>) | (lit<"y"> + ref<T2>)> {};
};
}  // namespace grown

}  // namespace trank

// The state a cell stands in says which choice orders it: after `y c` only T2
// is open -- a further reading of `c` keeps `q` after it -- so `y c w` is T2's,
// which writes B first, although T1 alone ends on the same `w` and closes
// narrower.
namespace tcontext {

struct G : trank::Words {
  struct A
      : rule<create<trank::ast::AKid>(assign<&trank::ast::AKid::t>(ref<C>))> {};
  struct B
      : rule<create<trank::ast::BKid>(assign<&trank::ast::BKid::t>(ref<C>))> {};
  struct T1
      : rule<create<trank::ast::Doc>(assign<&trank::ast::Doc::kid>(ref<A>) |
                                     assign<&trank::ast::Doc::kid>(ref<B>))> {};
  struct T2 : rule<create<trank::ast::Doc>(
                  (ref<C> + lit<"q">) | assign<&trank::ast::Doc::kid>(ref<B>) |
                  assign<&trank::ast::Doc::kid>(ref<A>))> {};
  struct S : rule<(lit<"x"> + ref<T1> + lit<"w">) |
                  (lit<"y"> + ref<T2> + lit<"w">)> {};
};

}  // namespace tcontext

// A cell whose two readings die on the same token, one after more
// reductions than the other: `c x #` refutes `A` and `W` alike, so the
// look-ahead answers the arm the grammar prefers, as a valid text would.
namespace tphase {

namespace ast {
struct Kid : cursiv::Extends<cursiv::AstNode> {};
struct AKid : cursiv::Extends<Kid> {
  string_view t;
};
struct BKid : cursiv::Extends<Kid> {
  string_view t;
};
struct Wrap : cursiv::Extends<Kid> {
  Kid* kid = nullptr;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  Kid* kid = nullptr;
};
}  // namespace ast

struct G : trank::Words {
  struct A : rule<create<ast::AKid>(assign<&ast::AKid::t>(ref<C>))> {};
  struct B : rule<create<ast::BKid>(assign<&ast::BKid::t>(ref<C>))> {};
  struct W : rule<create<ast::Wrap>(assign<&ast::Wrap::kid>(ref<B>))> {};
  struct S : rule<create<ast::Doc>(
                 (assign<&ast::Doc::kid>(ref<A>) + lit<"x"> + lit<"y"> +
                  lit<"q">) |
                 (assign<&ast::Doc::kid>(ref<W>) + lit<"x"> + lit<"y"> +
                  lit<"r">))> {};
};

}  // namespace tphase

// A word that is a keyword in one alternative and a name in another, at one
// state, decided by what follows.
namespace tsoft {

namespace ast {
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> names;
};
}  // namespace ast

struct Words : tpasses::Words {
  struct NUM : terminal<some(d)> {};
};

// The same word as a named terminal on one side and an anonymous spelling
// on the other: two terminals, one spelling.
namespace named {
struct G : Words {
  struct TYPE : terminal<kw<"type">> {};
  static constexpr auto Name = append<&ast::Doc::names>(ref<ID> | ref<TYPE>);
  struct Root : rule<create<ast::Doc>(
                    some((kw<"type"> + Name + lit<"="> + Name) |
                             (Name + lit<"="> + ref<NUM>),
                         lit<";">))> {};
};
}  // namespace named

// The keyword spelled once, anonymous, on both sides. Behind a brace, the
// keyword against the plain identifier, no name rule admitting it: the state
// reads both, and the reserve gives the keyword the spelling.
namespace spelled {
struct G : Words {
  static constexpr auto Name = append<&ast::Doc::names>(ref<ID> | kw<"type">);
  static constexpr auto Prop =
      (kw<"get"> + append<&ast::Doc::names>(ref<ID>) + lit<"{"> + lit<"}">) |
      (append<&ast::Doc::names>(ref<ID>) + lit<":"> + ref<NUM>);
  struct Root : rule<create<ast::Doc>(
                    some((kw<"type"> + Name + lit<"="> + Name) |
                             (Name + lit<"="> + ref<NUM>),
                         lit<";">) |
                    (lit<"{"> + some(Prop, lit<",">) + lit<"}">))> {};
};
}  // namespace spelled

}  // namespace tsoft

namespace tpasses {
struct G : Words {
  struct Case
      : rule<(kw<"min"> + ref<tmin::G::R>) | (kw<"end"> + ref<tend::G::Doc>) |
             (kw<"peek"> + ref<tpeek::G::TopR>) |
             (kw<"repeat"> + ref<trepeat::G::Root>) |
             (kw<"wide"> + ref<twidealt::G::Root>) |
             (kw<"plain"> + ref<trank::plain::G::S>) |
             (kw<"grown"> + ref<trank::grown::G::S>) |
             (kw<"context"> + ref<tcontext::G::S>) |
             (kw<"settle"> + ref<tphase::G::S>) |
             (kw<"soft"> + ref<tsoft::spelled::G::Root>) |
             (kw<"named"> + ref<tsoft::named::G::Root>) |
             (kw<"longrun"> + ref<tbounds::longrun::G::S>) |
             ref<tempty::G::Root>> {};
  using entry = Case;
  using ignored = WS;
};
}  // namespace tpasses
