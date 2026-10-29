// The same, in a section: the rescuing letter is its grammar's class, which the
// host's says nothing of.
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
