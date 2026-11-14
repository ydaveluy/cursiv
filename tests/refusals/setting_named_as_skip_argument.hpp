// `ignore` is the argument of skip(); the setting is `ignored`. Declared this
// way the grammar would skip nothing.
// expect LR at compile: this grammar declares `ignore` or `hide` as a setting
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>))> {};
  using entry = Root;
  using ignore = WS;
};

}  // namespace refuse
