// A hidden trivia of twelve arms, each read where its own assertion holds: the
// check would walk every set of them that may hold at once, twice as many with
// each arm, so it stops and the grammar is generated with a warning.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct NOTE
      : terminal<(&lit<"#0"> + lit<"#0">) | (&lit<"#1"> + lit<"#1">) |
                 (&lit<"#2"> + lit<"#2">) | (&lit<"#3"> + lit<"#3">) |
                 (&lit<"#4"> + lit<"#4">) | (&lit<"#5"> + lit<"#5">) |
                 (&lit<"#6"> + lit<"#6">) | (&lit<"#7"> + lit<"#7">) |
                 (&lit<"#8"> + lit<"#8">) | (&lit<"#9"> + lit<"#9">) |
                 (&lit<"#a"> + lit<"#a">) | (&lit<"#b"> + lit<"#b">)> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>))> {};
  using entry = Root;
  using ignored = WS;
  using hidden = NOTE;
};

}  // namespace refuse
