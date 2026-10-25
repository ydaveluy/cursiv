// What both generators say to the author on stderr, each led by the grammar's
// name.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

#include "cursiv/engines/shared/gen/GrammarFault.hpp"
#include "cursiv/engines/shared/gen/LexRecursion.hpp"
#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"
#include "cursiv/engines/shared/gen/UnboundedCondition.hpp"
#include "cursiv/engines/shared/gen/stamp_split.hpp"
#include "cursiv/engines/shared/table/LexRelay.hpp"
#include "cursiv/engines/shared/table/StampLayout.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

struct GrammarImage;

// The dead branches, flags that may read nothing and repetitions of a run
// that a grammar spells: a warning each.
void report_grammar_warnings(std::string_view grammar, GrammarImage& image);

// A condition a completion cannot step over, a warning each; `rule_name`
// answers the name of a rule index, empty where none stands.
template <class Cfg, class RuleName>
void report_unbounded_conditions(std::string_view grammar, const Cfg& c,
                                 RuleName rule_name) {
  for (const UnboundedCondition& u : c.unbounded)
    std::fprintf(stderr, "%.*s: warning: %s\n", static_cast<int>(grammar.size()),
                 grammar.data(),
                 unbounded_condition_text(c, rule_name(u.rule), u).c_str());
}

// An arm that begins and ends with its own rule, a refusal each: true where
// there was one.
bool refuse_self_bounded_arms(std::string_view grammar, GrammarImage& image);

// A terminal that matches the empty string is a token the lexer never hands
// back: `terminal` names the first one, empty where there is none.
bool refuse_empty_match(std::string_view grammar, std::string_view terminal);

// A fault the lowering or the build recorded: its code, then its sentence.
inline void refuse_fault(std::string_view grammar, const GrammarFault& fault,
                         const std::string& message) {
  const std::string_view code = grammar_fault_name(fault.code);
  std::fprintf(stderr, "%.*s: refused (%.*s)\n%s\n",
               static_cast<int>(grammar.size()), grammar.data(),
               static_cast<int>(code.size()), code.data(), message.c_str());
}

// The split of a record's stamp over a table of `rows` and `states`, `forced`
// being the row width a lane asks for: true where none holds.
inline bool refuse_unstamped(std::string_view grammar, StampLayout& stamp,
                             std::uint32_t rows, std::uint32_t states,
                             unsigned forced = 0) {
  if (const auto split = stamp_split(rows, states, forced)) {
    stamp = *split;
    return false;
  }
  std::fprintf(stderr, "%.*s: refused by %s\n", static_cast<int>(grammar.size()),
               grammar.data(), stamp_refusal(rows, states, forced).c_str());
  return true;
}

// The first terminal whose match is empty: one a mode's start accepts, or a
// relay the start launches ends where its automaton cannot see. `accepted`
// answers the terminal a state accepts, `kInvalidSymbol` where none.
template <class Built, class Accepted>
std::string_view empty_match_terminal(const Built& a, Accepted accepted) {
  const auto empty = lex_empty_nodes(a.cfg);
  for (std::uint32_t m = 0; m < a.nmodes; ++m) {
    const std::uint16_t start = a.modeStart[m];
    if (const SymbolId t = accepted(start); t != kInvalidSymbol)
      return a.cfg.term_name(t);
    for (auto r = a.dfaRelay[start]; r != kNoRelay; r = a.relays[r].next)
      if (empty[a.cfg.termBuild[a.relays[r].term].lex])
        return a.cfg.term_name(a.relays[r].term);
  }
  return {};
}

// What the lowering or the build recorded as refused: its sentence, else the
// limit that overflowed. True where there was one.
template <class Built>
bool refuse_unbuilt(std::string_view grammar, const Built& a,
                    std::string_view rule) {
  const int n = static_cast<int>(grammar.size());
  if (a.cfg.lowerWhy != LowerFail::none)
    std::fprintf(stderr, "%.*s: refused: %s\n", n, grammar.data(),
                 lower_fail_sentence(a.cfg, rule).c_str());
  else if (a.overflow)
    std::fprintf(stderr, "%.*s: refused by %s\n", n, grammar.data(),
                 a.overflowCap ? a.overflowCap
                               : "the lowering, which named no limit");
  else
    return false;
  return true;
}

}  // namespace cursiv
