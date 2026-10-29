// expect LR: this alternative can create two nodes
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct Wrap : cursiv::Extends<ast::Node> {
  ast::Node* inner{};
};

struct G : Base {
  struct Root : rule<unordered(create<ast::Node>(lit<"a">),
                               nest<&Wrap::inner>(lit<"b">))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
