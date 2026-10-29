// Two trivia that read `#!x`, one hidden behind an assertion and one ignored:
// they tie where the assertion holds, but the check does not read whether it
// does, so the grammar is generated with a warning.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct NOTE : terminal<lit<"#"> + &lit<"!"> + many(cr<R"(^\n)">)> {};
  struct SKIP : terminal<lit<"#"> + many(cr<R"(^\n)">)> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>))> {};
  using entry = Root;
  using ignored = list<WS, SKIP>;
  using hidden = NOTE;
};

}  // namespace refuse
