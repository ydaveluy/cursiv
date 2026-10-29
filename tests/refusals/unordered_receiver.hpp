// expect LR: this directive writes a member of a class the node in hand may not be
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct Wrap : cursiv::Extends<cursiv::AstNode> {
  cursiv::AstNode* inner{};
};

struct G : Base {
  struct Root
      : rule<create<ast::Node> + unordered(assign<&ast::Node::name>(lit<"a">),
                                           nest<&Wrap::inner>(lit<"b">))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
