#pragma once

// Two readings a bounded look-ahead cannot separate: the repetition between
// them is unbounded, so no budget tells `C 'a'* 'b'` from `D` until the last
// token. A cell no look-ahead settles is split, never guessed.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tunsettled {

namespace ast {
struct P0 : cursiv::Extends<cursiv::AstNode> {};
struct P1 : cursiv::Extends<cursiv::AstNode> {};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  P0* p0 = nullptr;
  P1* p1 = nullptr;
};
}  // namespace ast

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct C : rule<lit<"x">> {};
  struct D : rule<lit<"x"> + many(lit<"a">) + lit<"c">> {};
  // The cell on a record of two tokens: which rule the record reduces to is
  // read by the look-ahead from the text past it, or left to a branch.
  struct A0 : rule<create<ast::P0>(lit<"p"> + lit<"x">)> {};
  struct A1 : rule<create<ast::P1>(lit<"p"> + lit<"x">)> {};
  struct Root
      : rule<create<ast::Doc>(
            (ref<C> + many(lit<"a">) + lit<"b">) | ref<D> |
            (assign<&ast::Doc::p0>(ref<A0>) + many(lit<"a">) + lit<"o0">) |
            (assign<&ast::Doc::p1>(ref<A1>) + many(lit<"a">) + lit<"o1">))> {};
  using entry = Root;
  using ignore = WS;
};

}  // namespace tunsettled
