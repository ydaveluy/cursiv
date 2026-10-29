// A rule whose only production calls itself: it derives no finite text, so
// the branch that reaches it can never reduce.
// expect LR: refused .ruleWithoutProduction..*rule .Loop. can never match any text
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Chain : cursiv::Extends<cursiv::AstNode> {
  vector<Chain*> items;
};
}  // namespace ast
struct G : Base {
  struct Loop : rule<create<ast::Chain>(append<&ast::Chain::items>(ref<Loop>) +
                                        lit<"x">)> {};
  struct Root : rule<create<ast::Chain>(lit<"a">) | ref<Loop>> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
