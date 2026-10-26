#pragma once

// Expression contracts share whitespace and word-boundary policies. Keep the
// fold grammar's overrides and unprefixed inputs; other entries select their
// original rules, including their local comment scopes.

#include "engines/ll/CaretCasesGrammar.hpp"
#include "engines/ll/IndirectFoldGrammar.hpp"
#include "engines/ll/InfixLoopGrammar.hpp"
#include "engines/ll/OperatorConditionGrammar.hpp"
#include "engines/ll/PrefixFactorGrammar.hpp"

namespace tllexpressions {

struct Ll : tllindirect::Combined {
  struct Root
      : cursiv::Rule<cursiv::ref<tllindirect::Combined::Root> |
                     (cursiv::lit<"factor:"> +
                      cursiv::ref<tllprefix::Base::Root>) |
                     (cursiv::lit<"loop:"> +
                      cursiv::ref<tinfixloop::Combined::Root>) |
                     cursiv::ref<toperatorcondition::Suite::Root> |
                     cursiv::ref<tcaretcases::Ll::Root>> {};
  using entry = Root;
};

}  // namespace tllexpressions
