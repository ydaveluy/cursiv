#pragma once

// Completion contracts with the same whitespace and word-boundary policies
// share one parser. Each prefix selects the original rule and its lexing mode.

#include "engines/ll/CaretClimbGrammar.hpp"
#include "engines/ll/CaretReadingGrammar.hpp"
#include "engines/ll/CaretTailGrammar.hpp"

namespace tcaretcases {

struct Ll : cursiv::GrammarSpec {
  // Two rules read the same `%` and differ by the token after it: the choice
  // reads `->`, the position after the `-` does not.
  struct ByDash : rule<lit<"%">> {};
  struct ByArrow : rule<lit<"%">> {};
  // A condition lexes the text after it by itself: a run of `~` that the arms
  // read one at a time is one `Tildes` token to the condition.
  struct Tildes : terminal<some(lit<"~">)> {};
  struct Root
      : rule<(lit<"reading:"> + ref<tcaretreading::Base::Root>) |
             (lit<"spelled:"> + ref<tcaretreading::Spelled::Root>) |
             (lit<"climb:"> + ref<tcaretclimb::Base::Root>) |
             create<tcaretreading::Doc>(
                 lit<"join:"> + ((ref<ByDash> + lit<"-"> + lit<">">) |
                                 (ref<ByArrow> + lit<"->"> + lit<"!">))) |
             create<tcaretreading::Doc>(
                 lit<"cond:"> + &(lit<"|"> + ref<Tildes> + lit<"!">)+lit<"|"> +
                 some(lit<"~">) + lit<"!">) |
             create<tcaretreading::Doc>(
                 lit<"not:"> + !(lit<"|"> + ref<Tildes> + lit<"?">)+lit<"|"> +
                 some(lit<"~">) + (lit<"!"> | lit<"?">)) |
             skip(ignore<tlltail::Base::WS>, hide<tlltail::Base::BLOCK>,
                  lit<"tail:"> + ref<tlltail::Base::Expression> + eof)> {};
  using entry = Root;
  using ignored = tcaretreading::Base::WS;
};

}  // namespace tcaretcases
