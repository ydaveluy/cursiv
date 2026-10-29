// Two members of classes that share none: the implicit node has no class to
// be, and the rule is told to state one.
// expect LR: write members of unrelated classes: add create<T> to say which class to build
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Left : cursiv::Extends<cursiv::AstNode> {
  string_view a;
};
struct Right : cursiv::Extends<cursiv::AstNode> {
  string_view b;
};
}  // namespace ast
struct G : Base {
  struct Root
      : rule<assign<&ast::Left::a>(ref<ID>) + assign<&ast::Right::b>(ref<ID>)> {
  };
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
