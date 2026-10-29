// Tokens are not a node: a nest behind them has an empty node to bury.
// expect LR: has a nest<> with nothing before it
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Wrapper : cursiv::Extends<cursiv::AstNode> {
  cursiv::AstNode* inner = nullptr;
};
}  // namespace ast
struct G : Base {
  struct Root : rule<lit<"a"> +
                     many(nest<ast::Wrapper, &ast::Wrapper::inner>(lit<"!">))> {
  };
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
