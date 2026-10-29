// A call adopts its node while the current node is empty: after a write, the
// node the call returns has nowhere to take what was written.
// expect LR: calls a rule that builds a node after the node in hand was written, created or nested
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Item : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
}  // namespace ast
struct G : Base {
  struct Body : rule<create<ast::Item>(lit<"item">)> {};
  struct Root : rule<assign<&ast::Item::name>(ref<ID>) + ref<Body>> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
