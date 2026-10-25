#pragma once

// A one-letter token refusing the letters a longer one reads: at the end of
// the input the walk that refused it is still alive, and no token ends on
// it.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace topen {

namespace ast {
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> items;
};
}  // namespace ast

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct A : terminal<lit<"a", !lit<"bcd">>> {};
  struct L : terminal<some(cr<"b-e">)> {};
  struct Doc : rule<create<ast::Doc>(
                   many(append<&ast::Doc::items>(ref<A> | ref<L>)))> {};
  using entry = Doc;
  using ignore = WS;
};

}  // namespace topen
