// A left-recursive call inside a repetition: LR accepts it; LL accepts
// optional items in front of the call, not a repetition around it.
// expect LR: refusal_left_recursion_repeated: states=
// expect LL: refused .leftRecursion..*rule .Path. reaches itself before matching anything inside a repetition
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
