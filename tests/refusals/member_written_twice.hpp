// One single-valued member written twice on one path: the second overwrites
// the first, which is left with a parent that does not hold it.
// expect LR: [ '‘]WrittenAgain<.*refuse::G::Root.*refuse::ast::Holder::first
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Holder : cursiv::Extends<cursiv::AstNode> {
  Node* first = nullptr;
};
}  // namespace ast
struct G : Base {
  struct Item : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>))> {};
  struct Root : rule<create<ast::Holder>(
                    assign<&ast::Holder::first>(ref<Item>) + lit<","> +
                    assign<&ast::Holder::first>(ref<Item>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
