// A left-recursive call inside a repetition: LR reads it, the descent spells
// out only an option or a repetition in front of the call.
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
