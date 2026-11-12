// Unordered items where the `nest` also writes `name`: in one order, the other
// item writes `name` again on the same node.
// expect LR at compile: [ '‘]WrittenAgain<.*refuse::G::Root.*refuse::ast::Node::name
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
