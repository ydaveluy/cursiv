// Two trivia that read `#`, one only where `!` follows and one only where it
// does not: no text ties them, but the check does not read what an assertion
// tests, so the grammar is generated with a warning.
// expect LR: warning: terminals .(MARK|NOTE). and .(MARK|NOTE). are trivia, and the check that they never read the same text at the same length and rank could not be completed
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
