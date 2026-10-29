#pragma once

#include "refusals/head.hpp"

namespace refuse {
struct G : Base {
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>))> {};
  using entry = Root;
  using ignored = WS;
  using hidden = WS;
};
}  // namespace refuse
