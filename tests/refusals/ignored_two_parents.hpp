// Two parents, one of which skips white space: the child sees two answers to
// what it skips, and says neither.
// expect LR at compile: two parent grammars declare different .ignored. lists
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct Spaced : Base {
  struct Top : rule<create<ast::Node>(kw<"if"> +
                                      assign<&ast::Node::name>(ref<ID>))> {};
  using entry = Top;
  using ignored = WS;
};
struct Other : Base {
  struct NUM : terminal<some(d)> {};
};

struct G : cursiv::extends<Spaced, Other> {
  using entry = Spaced::Top;
};

}  // namespace refuse
