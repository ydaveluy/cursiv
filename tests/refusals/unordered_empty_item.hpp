// An unordered item that may read nothing and is no optional item could stand
// anywhere in the order: both engines refuse the group, naming the item.
// expect LR LL: refused .nullableUnorderedItem..*rule .Root. has an unordered item that may match nothing, ."final"[?] "static"[?].: an item that matches nothing
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Root : rule<create<ast::Node>(
                    kw<"node"> +
                    unordered(assign<&ast::Node::name>(ref<ID>),
                              option(kw<"final">) + option(kw<"static">)))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
