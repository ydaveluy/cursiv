// A bracket changes how the lines after it read: the text must spell it.
// expect LR LL: in .implied.tok, cond.., .tok. cannot be a bracket named by
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Root : rule<create<ast::Node>(implied(lit<"(">, gap::line) +
                                       assign<&ast::Node::name>(ref<ID>))> {};
  using entry = Root;
  using ignored = WS;
  static constexpr auto indent_open = lit<"(">;
};

}  // namespace refuse
