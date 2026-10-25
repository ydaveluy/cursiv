#pragma once

// A literal with a boundary of its own: `hello` may not be followed by `;`.
// What the lexer refuses, a repair may not invent.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tbnd {

namespace ast {
struct Doc : cursiv::Extends<cursiv::AstNode> {
  std::string_view name;
};
}  // namespace ast

struct BoundaryG : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};
  struct HELLO : terminal<lit<"hello", !cr<";">>> {};
  struct Root
      : rule<create<ast::Doc>(ref<HELLO> + assign<&ast::Doc::name>(ref<ID>))> {
  };
  using entry = Root;
  using ignore = WS;
};

}  // namespace tbnd
