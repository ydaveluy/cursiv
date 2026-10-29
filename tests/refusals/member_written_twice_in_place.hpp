// A node created in place, as the operand of `assign`, writes one of its
// single-valued members twice: the second overwrites the first.
// expect LR: [ '‘]WrittenAgain<.*refuse::G::Root.*refuse::ast::Node::name
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Holder : cursiv::Extends<cursiv::AstNode> {
  Node* part = nullptr;
};
}  // namespace ast
struct G : Base {
  struct Root : rule<create<ast::Holder>(assign<&ast::Holder::part>(
                    create<ast::Node>(assign<&ast::Node::name>(ref<ID>) +
                                      lit<","> +
                                      assign<&ast::Node::name>(ref<ID>))))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
