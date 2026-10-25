#pragma once

// A grammar whose only terminal is a trivia: the state that reads the end
// and the repair's mode lex the same alphabet, so the table has one mode.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tonemode {

namespace ast {
struct Doc : cursiv::Extends<cursiv::AstNode> {
  bool empty = false;
};
}  // namespace ast

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t\n)">)> {};
  struct Doc : rule<create<ast::Doc>(eof)> {};
  using entry = Doc;
  using ignore = WS;
};

}  // namespace tonemode
