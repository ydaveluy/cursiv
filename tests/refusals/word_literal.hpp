// A literal with no guard that ends in a word character reads the start of a
// longer word: the grammar is read, and the generator says so.
// expect LR LL: warning: rule .Root.: .'begin'. is a literal with no guard that ends in a word character, so it also matches the start of a longer word.*write .kw<.begin.>.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Root : rule<create<ast::Node>(lit<"begin"> +
                                       assign<&ast::Node::name>(ref<ID>) +
                                       kw<"end">)> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
