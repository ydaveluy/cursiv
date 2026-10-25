// The item in front of the recursion is a condition, which reads no text.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct X : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  vector<X*> body;
};
}  // namespace ast

struct G : Base {
  struct X : rule<create<ast::X>(!lit<"q"> + append<&ast::X::body>(ref<X>) +
                                 lit<"+">) |
                  create<ast::X>(assign<&ast::X::name>(ref<ID>))> {};
  using entry = X;
  using ignore = WS;
};

}  // namespace refuse
