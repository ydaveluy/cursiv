#pragma once

// Rule/terminal conditions and guarded keyword readings share their word
// boundary. Only the rule-condition entry retains block comments as trivia.
// The standalone refused-keyword table remains separate for raw lexer tests.

#include "engines/ll/ReservedRefusedGlrGrammar.hpp"
#include "engines/ll/ReservedRefusedPeekGrammar.hpp"
#include "engines/ll/RuleConditionGrammar.hpp"

namespace tllguardcases {

struct Ll : cursiv::GrammarSpec {
  struct Root
      : rule<skip(ignore<tllrcond::Base::WS>, hide<tllrcond::Base::CMT>,
                  lit<"rules:"> + ref<tllrcond::Base::Root>) |
             (lit<"terms:"> + ref<tllterm::Base::Root>) |
             (lit<"several:"> + ref<tllrefusedglr::Base::Root>) |
             (lit<"peek:"> + ref<tllrefusedpeek::Base::Root>)> {};
  using entry = Root;
  using ignored = tllterm::Base::WS;
};

}  // namespace tllguardcases
