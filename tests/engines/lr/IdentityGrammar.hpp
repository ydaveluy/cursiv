#pragma once

// A grammar the merge breaks: `A -> c` and `B -> c` reduce under lookaheads
// that swap between the `a` and `b` contexts, so the core graph fuses them
// into one state and arbitrates by declaration order -- two of the four
// sentences die.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tident {

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

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct C : terminal<lit<"c">> {};
  struct A : rule<create<ast::AKid>(assign<&ast::AKid::t>(ref<C>))> {};
  struct B : rule<create<ast::BKid>(assign<&ast::BKid::t>(ref<C>))> {};
  struct S : rule<create<ast::Doc>(
                 (lit<"a"> + assign<&ast::Doc::kid>(ref<A>) + lit<"d">) |
                 (lit<"b"> + assign<&ast::Doc::kid>(ref<B>) + lit<"d">) |
                 (lit<"a"> + assign<&ast::Doc::kid>(ref<B>) + lit<"e">) |
                 (lit<"b"> + assign<&ast::Doc::kid>(ref<A>) + lit<"e">))> {};
  using entry = S;
  using ignore = WS;
};

}  // namespace tident
