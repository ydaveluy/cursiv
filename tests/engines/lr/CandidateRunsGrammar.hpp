#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tcandrun {

struct Doc : cursiv::Extends<cursiv::AstNode> {};

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Word : terminal<some(cr<"a-z">)> {};
  struct Twin : terminal<cr<"a-z"> + many(cr<"a-z">)> {};
  struct Quoted : terminal<lit<"\""> + many(cr<R"(^")">) + lit<"\"">> {};
  struct OtherQuoted : terminal<lit<"\""> + many(cr<R"(^")">) + lit<"\"">> {};
  struct Longer : terminal<some(cr<"a-z">) + lit<"::">> {};
  struct Bounded : terminal<lit<"@"> + some(lit<"g", !cr<";">>)> {};
  struct OtherBounded : terminal<lit<"@"> + some(lit<"g", !cr<";">>)> {};
  struct Root : rule<create<Doc>(many(
                    (ref<Word> + lit<"!">) | (ref<Twin> + lit<"?">) |
                    (ref<Quoted> + lit<"!">) | (ref<OtherQuoted> + lit<"?">) |
                    (ref<Longer> + lit<"!">) | (kw<"while"> + lit<";">) |
                    (ref<Bounded> + lit<"!">) | (ref<OtherBounded> + lit<"?">)))> {};
  using entry = Root;
  using ignore = WS;
};

}  // namespace tcandrun
