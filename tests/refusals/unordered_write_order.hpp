#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct Wrap : cursiv::Extends<ast::Node> {
  ast::Node* inner{};
};

struct G : Base {
  struct Root : rule<create<Wrap> +
                     unordered(assign<&ast::Node::name>(lit<"a">),
                               nest<&Wrap::inner>(
                                   assign<&ast::Node::name>(lit<"b">)))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
