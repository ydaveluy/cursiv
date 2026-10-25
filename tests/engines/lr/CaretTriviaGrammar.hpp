#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tcarettrivia {

struct Doc : cursiv::Extends<cursiv::AstNode> {};

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t)"> | (lit<"\\"> + eol))> {};
  struct NL : terminal<eol> {};
  struct COMMENT : terminal<lit<"//"> + many(cr<R"(^\r\n)">)> {};
  struct Root : rule<create<Doc>(
                    many(kw<"go"> + (kw<"north"> | kw<"south">)+lit<";">))> {};
  using entry = Root;
  using ignore = list<WS, NL, COMMENT>;
};

}  // namespace tcarettrivia
