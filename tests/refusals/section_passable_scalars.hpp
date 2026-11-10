// In a section, a match that may end where a word its guard excludes ends: a
// following `é`, only in the embedded grammar's word boundary, lets the guard
// pass.
// expect LR: a character past ASCII after that word lets the guard pass
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct Inner : cursiv::GrammarSpec {
  struct SP : terminal<some(cr<" ">)> {};
  struct W : terminal<some(cr<"x">) + !kw<"if"> + some(cr<"a-z">)> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<W>))> {};
  using entry = Root;
  using ignored = SP;
  static constexpr auto word_boundary = cr<R"(a-zé)">;
};

struct G : Base {
  struct Root : rule<section<Inner>(lit<"<">, lit<">">)> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
