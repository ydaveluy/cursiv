// The complement of `any` holds no character.
// expect LR: this complement matches no character
#pragma once

#include "refusals/head.hpp"

namespace refuse {
struct G : Base {
  struct NAME : terminal<~any> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<NAME>))> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace refuse
