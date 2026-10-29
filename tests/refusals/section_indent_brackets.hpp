// A grammar that names the brackets indentation is blind inside of, embedded:
// its lines would be measured against the embedding document's.
// expect LR: a grammar embedded with .section<>. reads no indentation
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct Inner : cursiv::GrammarSpec {
  struct SP : terminal<some(cr<" ">)> {};
  struct NUM : terminal<some(d)> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<NUM>))> {};
  using entry = Root;
  using ignored = SP;
  static constexpr auto indent_open = lit<"(">;
  static constexpr auto indent_close = lit<")">;
};

struct G : Base {
  struct Root : rule<section<Inner>(lit<"<">, lit<">">)> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
