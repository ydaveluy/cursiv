// Two trivia that read `#`, one only where `!` follows and one only where it
// does not: no text ties them, but the check does not read what an assertion
// tests, so the grammar is generated with a warning.
// expect LR: warning: terminals .(MARK|NOTE). and .(MARK|NOTE). are trivia, but the generator could not check that they never match the same text with the same length and category
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
