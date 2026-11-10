// The base of every refusal fixture: one skipper and one name. Nothing runs:
// ctest matches what the generation prints against the `// expect` lines.
#pragma once

#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"

namespace refuse {

namespace ast {
struct Node : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
}  // namespace ast

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};
};

}  // namespace refuse
