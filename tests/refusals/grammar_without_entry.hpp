// A grammar that names no entry rule: nothing says where a document starts.
// expect LR at compile: this grammar names no entry rule
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>))> {};
  using ignored = WS;
};

}  // namespace refuse
