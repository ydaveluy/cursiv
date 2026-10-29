#pragma once

// A word that is a guarded terminal in one arm and a name in the other: `do`
// read by the guarded terminal, or by the name that the second arm goes on
// from. Both are patterns, so the lexer leaves the choice to the parse; a
// literal would outrank the name. A caret after the word offers what either
// reading goes on with.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tcaretreading {

struct Doc : cursiv::Extends<cursiv::AstNode> {};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct DO : terminal<cr<"d"> + cr<"o"> + !lit<"!">> {};
  struct Root : rule<create<Doc>((ref<DO> + (lit<"!"> | lit<"?">)) |
                                 (ref<ID> + lit<":">))> {};
  using entry = Root;
  using ignored = WS;
};

struct Spelled : Base {
  struct Root
      : rule<create<Doc>((lit<"do", !lit<"!">> + (lit<"!"> | lit<"?">)) |
                         (ref<ID> + lit<":">))> {};
  using entry = Root;
};
}  // namespace tcaretreading
