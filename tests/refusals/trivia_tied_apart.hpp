// Two trivia that read `#`, one only where `!` follows and one only where it
// does not: no text ties them, but the check does not read what an assertion
// tests, so the grammar is generated with a warning.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct MARK : terminal<lit<"#"> + &lit<"!">> {};
  struct NOTE : terminal<lit<"#"> + !lit<"!">> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>))> {};
  using entry = Root;
  using ignored = list<WS, MARK>;
  using hidden = NOTE;
};

}  // namespace refuse
