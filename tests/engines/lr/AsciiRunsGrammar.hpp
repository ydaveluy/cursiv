#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tasciiruns {

namespace ast {
struct Doc : cursiv::Extends<cursiv::AstNode> {};
}  // namespace ast

struct AsciiRunsGrammar : cursiv::GrammarSpec {
  struct Hex : terminal<some(cr<"0-9A-Fa-f">)> {};
  struct High : terminal<some(cr<R"(A-Fa-f\u{80}-\u{10FFFF})">)> {};
  struct Asymmetric : terminal<some(cr<"0-9A-D_a-c">)> {};
  struct Byte : terminal<some(byte)> {};
  struct Nul : terminal<some(cr<R"(\x00)">)> {};
  struct Entry : rule<create<ast::Doc>(
                     (lit<"hex:"> + ref<Hex>) | (lit<"high:"> + ref<High>) |
                     (lit<"asym:"> + ref<Asymmetric>) |
                     (lit<"byte:"> + ref<Byte>) | (lit<"nul:"> + ref<Nul>))> {};
  using entry = Entry;
};

}  // namespace tasciiruns
