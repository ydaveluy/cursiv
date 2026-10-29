// A rule whose only reading asserts end of input and then reads on: the
// reading is dropped, and the rule is left with none.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Holder : cursiv::Extends<cursiv::AstNode> {
  Node* tail = nullptr;
};
}  // namespace ast

struct G : Base {
  struct Tail
      : rule<create<ast::Node>(eof + assign<&ast::Node::name>(ref<ID>))> {};
  struct Root : rule<create<ast::Holder>(
                    lit<"x"> + assign<&ast::Holder::tail>(ref<Tail>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
