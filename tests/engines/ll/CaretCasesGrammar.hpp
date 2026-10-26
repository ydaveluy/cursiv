#pragma once

// Completion contracts with the same whitespace and word-boundary policies
// share one parser. Each prefix selects the original rule and its lexing mode.

#include "engines/ll/CaretClimbGrammar.hpp"
#include "engines/ll/CaretReadingGrammar.hpp"
#include "engines/ll/CaretTailGrammar.hpp"

namespace tcaretcases {

struct Ll : cursiv::GrammarSpec {
  struct Root
      : rule<(lit<"reading:"> + ref<tcaretreading::Base::Root>) |
             (lit<"spelled:"> + ref<tcaretreading::Spelled::Root>) |
             (lit<"climb:"> + ref<tcaretclimb::Base::Root>) |
             skip(ignored<tlltail::Base::WS>, hide<tlltail::Base::BLOCK>,
                  lit<"tail:"> + ref<tlltail::Base::Expression> + eof)> {};
  using entry = Root;
  using ignore = tcaretreading::Base::WS;
};

}  // namespace tcaretcases
