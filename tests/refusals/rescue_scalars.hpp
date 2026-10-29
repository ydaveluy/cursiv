// A match that ends where a word it refuses ends, past a head, and a letter
// past ASCII rescues that word: the body does not read on over it.
// expect LR: a character past ASCII after that word lets the guard pass
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct W : terminal<some(cr<"x">) + !kw<"if"> + some(cr<"a-z">)> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<W>))> {};
  using entry = Root;
  using ignored = WS;
  static constexpr auto word_boundary = cr<R"(a-zé)">;
};

}  // namespace refuse
