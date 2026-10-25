// One word, two hover texts: a token has one.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Root
      : rule<create<ast::Node>((kw<"if">.doc<"opens a branch">() +
                                assign<&ast::Node::name>(ref<ID>)) |
                               (kw<"if">.doc<"a conditional">() + lit<"!"> +
                                assign<&ast::Node::name>(ref<ID>)))> {};
  using entry = Root;
  using ignore = WS;
};

}  // namespace refuse
