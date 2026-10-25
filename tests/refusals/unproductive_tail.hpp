// A rule whose only production calls itself after a token: no left recursion,
// and still no finite text.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Chain : cursiv::Extends<cursiv::AstNode> {
  vector<Chain*> items;
};
}  // namespace ast
struct G : Base {
  struct Loop : rule<create<ast::Chain>(lit<"x"> +
                                        append<&ast::Chain::items>(ref<Loop>))> {
  };
  struct Root : rule<create<ast::Chain>(lit<"a">) | ref<Loop>> {};
  using entry = Root;
  using ignore = WS;
};

}  // namespace refuse
