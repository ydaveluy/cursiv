// A directive over a creation holding a second one: the directive writes one
// node and the other is lost.
// expect LR LL: a create<> or nest<> here starts a second node inside it
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Holder : cursiv::Extends<cursiv::AstNode> {
  Node* one = nullptr;
};
struct Other : cursiv::Extends<Node> {};
}  // namespace ast

struct G : Base {
  struct Root : rule<create<ast::Holder>(
                    lit<"h"> + assign<&ast::Holder::one>(create<ast::Node>(
                                   create<ast::Other>(lit<"k">))))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
