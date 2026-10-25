#pragma once

#include "refusals/head.hpp"

namespace refuse {
struct G : Base {
  struct BadSkip : rule<some(s)> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>))> {};
  using entry = Root;
  using ignore = list<WS, BadSkip>;
};
}  // namespace refuse
