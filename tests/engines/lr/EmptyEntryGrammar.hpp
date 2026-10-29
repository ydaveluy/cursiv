#pragma once

// A nullable entry: its empty production stands in the production pool, and
// the closure must not read the next production's first slot as its own.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tempty {

namespace ast {
struct Doc : cursiv::Extends<cursiv::AstNode> {
  string_view m;
};
}  // namespace ast

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct MT : terminal<lit<"m">> {};
  struct M : rule<assign<&ast::Doc::m>(ref<MT>)> {};
  struct N : rule<ref<M> + lit<"x">> {};
  struct Root : rule<(lit<"k"> + ref<N>) | create<ast::Doc>(eof)> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace tempty
