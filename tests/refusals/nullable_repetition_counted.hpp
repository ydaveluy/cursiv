// A counted repetition of a rule that may match nothing, which only the build
// sees, is spelt out as a ladder nested as deep as its bound: the refusal is
// told without walking it.
// expect LL: refused .nullableRepetition..*rule .Root. repeats an item that may match nothing
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Item : rule<option(ref<ID>)> {};
  struct Root : rule<create<ast::Node>(repeat<0, 21000>(ref<Item>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
