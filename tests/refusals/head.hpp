// The grammar every refusal lane starts from: one skipper, one name, and
// whatever the lane spells over them. A refusal is a refused generation, so a
// lane has nothing to run -- what ctest reads is the sentence the refusal
// carries.
#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace refuse {

namespace ast {
struct Node : cursiv::Extends<cursiv::AstNode> {
  cursiv::AstNode::string_view name;
};
}  // namespace ast

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};
};

}  // namespace refuse
