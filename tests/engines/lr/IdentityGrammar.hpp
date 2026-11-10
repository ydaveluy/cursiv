#pragma once

// A grammar the merge breaks: `A -> c` and `B -> c` reduce under lookaheads
// that swap between the `a` and `b` contexts, so the core graph fuses them,
// arbitrates by declaration order, and two of the four sentences die.

#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"

namespace tident {

namespace ast {
struct Child : cursiv::Extends<cursiv::AstNode> {};
struct AChild : cursiv::Extends<Child> {
  string_view t;
};
struct BChild : cursiv::Extends<Child> {
  string_view t;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  Child* child = nullptr;
};
}  // namespace ast

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct C : terminal<lit<"c">> {};
  struct A : rule<create<ast::AChild>(assign<&ast::AChild::t>(ref<C>))> {};
  struct B : rule<create<ast::BChild>(assign<&ast::BChild::t>(ref<C>))> {};
  struct S : rule<create<ast::Doc>(
                 (lit<"a"> + assign<&ast::Doc::child>(ref<A>) + lit<"d">) |
                 (lit<"b"> + assign<&ast::Doc::child>(ref<B>) + lit<"d">) |
                 (lit<"a"> + assign<&ast::Doc::child>(ref<B>) + lit<"e">) |
                 (lit<"b"> + assign<&ast::Doc::child>(ref<A>) + lit<"e">))> {};
  using entry = S;
  using ignored = WS;
};

}  // namespace tident
