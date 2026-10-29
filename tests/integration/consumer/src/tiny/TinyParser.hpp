// A grammar of the consumer's own -- no cursiv example is reachable from here.
// Small on purpose: what this project proves is the build surface, not a
// language.
#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "tiny/ast.hpp"

namespace tiny {

struct Tiny : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t\n\r)">)> {};
  struct NUM : terminal<as<int>(some(cr<"0-9">))> {};

  struct TermRule : rule<assign<&ast::Term::value>(ref<NUM>)> {};
  struct SumRule
      : rule<append<&ast::Sum::terms>(ref<TermRule>) +
             some(lit<"+"> + append<&ast::Sum::terms>(ref<TermRule>))> {};

  using entry = SumRule;
  using ignored = WS;
};

}  // namespace tiny
