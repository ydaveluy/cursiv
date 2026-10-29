// Two parents, one of which declares its own word boundary: the child sees
// two answers to one question, and its keywords would take neither.
// expect LR: two parents of this grammar declare different
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct Dashed : Base {
  static constexpr auto word_boundary = cr<"a-zA-Z0-9_-">;
  struct Top : rule<create<ast::Node>(kw<"if"> +
                                      assign<&ast::Node::name>(ref<ID>))> {};
  using entry = Top;
  using ignored = WS;
};
struct Other : Base {
  struct NUM : terminal<some(d)> {};
};

struct G : cursiv::extends<Dashed, Other> {
  using entry = Dashed::Top;
  using ignored = WS;
};

}  // namespace refuse
