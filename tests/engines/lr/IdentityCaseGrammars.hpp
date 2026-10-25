#pragma once

// Grammars the identity choice is asked about: it builds the wider graph
// only where the merge may have made a conflict, and that graph fuses the
// states a real conflict leaves alike.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tidcase {

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

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct C : terminal<lit<"c">> {};
  struct A : rule<create<ast::AKid>(assign<&ast::AKid::t>(ref<C>))> {};
  struct B : rule<create<ast::BKid>(assign<&ast::BKid::t>(ref<C>))> {};
};

// `p c x` is `A` or `B` until the token after `x`: one context calls both
// and reads `x` after each, so every canonical graph holds the conflict.
// `q c x` writes them the other way round: nothing written orders the cell.
namespace lr2 {
struct G : Base {
  struct S
      : rule<create<ast::Doc>(
            (lit<"p"> + assign<&ast::Doc::kid>(ref<A>) + lit<"x"> + lit<"z">) |
            (lit<"p"> + assign<&ast::Doc::kid>(ref<B>) + lit<"x"> + lit<"w">) |
            (lit<"q"> + assign<&ast::Doc::kid>(ref<B>) + lit<"x"> + lit<"z">) |
            (lit<"q"> + assign<&ast::Doc::kid>(ref<A>) + lit<"x"> +
             lit<"w">))> {};
  using entry = S;
  using ignore = WS;
};
}  // namespace lr2

// The merge's conflict on `c`, and a real one -- the `e` that `i i X e X`
// gives to either `i` -- reached from `x` and from `z` with different
// lookaheads: the two canonical copies of the real one hold the same cell, and
// the wider graph keeps one state for both.
namespace sum {
struct E : cursiv::Extends<ast::Kid> {
  ast::Kid* l = nullptr;
  ast::Kid* r = nullptr;
};
struct G : Base {
  struct X : rule<create<E>(lit<"i"> + assign<&E::l>(ref<X>) +
                            option(lit<"e"> + assign<&E::r>(ref<X>))) |
                  ref<A>> {};
  struct S : rule<create<ast::Doc>(
                 (lit<"a"> + assign<&ast::Doc::kid>(ref<A>) + lit<"d">) |
                 (lit<"b"> + assign<&ast::Doc::kid>(ref<B>) + lit<"d">) |
                 (lit<"a"> + assign<&ast::Doc::kid>(ref<B>) + lit<"e">) |
                 (lit<"b"> + assign<&ast::Doc::kid>(ref<A>) + lit<"e">) |
                 (lit<"x"> + assign<&ast::Doc::kid>(ref<X>) + lit<"y">) |
                 (lit<"z"> + assign<&ast::Doc::kid>(ref<X>) + lit<"w">))> {};
  using entry = S;
  using ignore = WS;
};
}  // namespace sum

}  // namespace tidcase
