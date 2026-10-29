// An AST member that owns its text: the tree borrows the document.
#pragma once

#include <string>

#include "refusals/head.hpp"

namespace refuse {

namespace owner {
struct Node : cursiv::Extends<cursiv::AstNode> {
  std::string name;
};
}  // namespace owner
struct G : Base {
  struct Root : rule<create<owner::Node>(assign<&owner::Node::name>(ref<ID>))> {
  };
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
