#pragma once

// A token that spans lines and ends in a letter: the word under a caret can
// then reach past the caret's line, which a completion edit may not.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tblock {

namespace ast {
struct Item : cursiv::Extends<cursiv::AstNode> {};
}  // namespace ast

struct Block : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct BLOCK : terminal<through(lit<"begin">, lit<"end">)> {};
  struct ItemR : rule<create<ast::Item>(ref<BLOCK> | kw<"entity">)> {};
  using entry = ItemR;
  using ignored = WS;
};

}  // namespace tblock
