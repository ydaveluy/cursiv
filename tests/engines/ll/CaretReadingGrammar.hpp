#pragma once

// A word that is a guarded terminal in one arm and a name in the other: `do`.
// Both are patterns, so the parse decides; a caret after the word offers what
// either reading goes on with.

#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"

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
