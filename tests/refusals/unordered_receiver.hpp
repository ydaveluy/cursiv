// A `create`, then unordered items with a `nest`: after the nest, `name` would
// be written on a `Wrap`, which has no such member.
// expect LR: this directive writes a member that the current node may not have
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
