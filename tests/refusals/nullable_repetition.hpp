// A repetition whose item may match nothing: the loop has no fixpoint.
// expect LR: refused .nullableRepetition..*rule .Root. repeats an item that may match nothing
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Root : rule<create<ast::Node>(many(option(ref<ID>)))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
