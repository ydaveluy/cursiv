#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct Inner : cursiv::GrammarSpec {
  struct REGION : terminal<lit<"E"> + option(ref<REGION>) + lit<"ND">> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<REGION>))> {
  };
  using entry = Root;
};

struct G : Base {
  struct Root : rule<section<Inner>(lit<"<">, lit<"END">)> {};
  using entry = Root;
};

}  // namespace refuse
