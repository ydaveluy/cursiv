// A counted repetition, up to 21 000, of a rule that may match nothing: LL
// refuses it before expanding the bound into nested levels.
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
