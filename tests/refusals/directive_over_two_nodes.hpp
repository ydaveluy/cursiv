// A directive over a qualifier and a name: two nodes, one member.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Typed : cursiv::Extends<cursiv::AstNode> {
  Node* name = nullptr;
};
}  // namespace ast

struct G : Base {
  struct Name : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>))> {};
  struct Qualifier : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>) +
                                            lit<"::">)> {};
  struct Root : rule<create<ast::Typed>(assign<&ast::Typed::name>(
                    option(ref<Qualifier>) + ref<Name>))> {};
  using entry = Root;
  using ignore = WS;
};

}  // namespace refuse
