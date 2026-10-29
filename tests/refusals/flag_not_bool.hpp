// `flag` sets a flag: the member it names is a bool.
// expect LR: flag<&T::m>: the member must be bool
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Root : rule<create<ast::Node>(flag<&ast::Node::name>(kw<"on">))> {
  };
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
