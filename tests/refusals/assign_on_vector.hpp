// `=` names the one value a member takes: on a `vector<T>` the matches would
// pile up, which is what `+=` says.
// expect LR: sets the single value of a member, and this member is a .vector<T>. list
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Bag : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> names;
};
}  // namespace ast

struct G : Base {
  struct Root
      : rule<create<ast::Bag>(many(assign<&ast::Bag::names>(ref<ID>)))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
