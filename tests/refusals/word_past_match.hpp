// A word refused behind one literal of a choice: the match of `##` ends before
// `abc` is read, so whether it stands turns on text past the token.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct HASH : terminal<lit<"#"> | lit<"##", !lit<"abc">> |
                         lit<"##a", !lit<"bc">>> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<HASH>) +
                                     many(ref<HASH> | ref<ID>))> {};
  using entry = Root;
};

}  // namespace refuse
