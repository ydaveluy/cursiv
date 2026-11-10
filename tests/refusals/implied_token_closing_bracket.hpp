// An implied closing bracket: refused even when `implied` names it by a
// terminal and `indent_close` by a literal.
// expect LR LL: in .implied.tok, cond.., .tok. cannot be a bracket named by
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct RPAR : terminal<lit<")">> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>) +
                                       implied(ref<RPAR>, gap::line))> {};
  using entry = Root;
  using ignored = WS;
  static constexpr auto indent_close = lit<")">;
};

}  // namespace refuse
