// A bracket named by a rule: a rule is no token, and nothing would mark it.
// expect LR: `indent_open` and `indent_close` name a terminal a rule reads
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct LPAR : terminal<lit<"(">> {};
  struct Open : rule<ref<LPAR>> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>) +
                                       ref<Open>)> {};
  using entry = Root;
  using ignored = WS;
  static constexpr auto indent_open = ref<Open>;
};

}  // namespace refuse
