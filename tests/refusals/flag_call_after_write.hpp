// A flag around a call changes nothing: the call adopts its node only while
// the current node is empty.
// expect LR: calls a rule that builds a node after the current node was written, created or nested
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Item : cursiv::Extends<cursiv::AstNode> {
  boolean on;
  string_view name;
};
}  // namespace ast
struct G : Base {
  struct Body : rule<create<ast::Item>(lit<"item">)> {};
  struct Root : rule<assign<&ast::Item::name>(ref<ID>) +
                     flag<&ast::Item::on>(lit<"("> + ref<Body> + lit<")">)> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
