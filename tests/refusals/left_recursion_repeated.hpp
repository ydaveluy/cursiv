// A left-recursive call inside a repetition: LR reads it, the descent spells
// out only an option or a repetition in front of the call.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Path : rule<many(ref<Path> + lit<".">) + ref<ID>> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<Path>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
