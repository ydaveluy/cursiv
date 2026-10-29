#include "cursiv/engines/ll/gen/LlFinalProgram.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include "cursiv/engines/shared/gen/GrammarFault.hpp"
#include "cursiv/engines/shared/gen/gen_report.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

namespace {

using AsWrittenFn = std::string (*)(std::string_view);

// A shape the descent cannot hold: the sentence names the rule, as its author
// wrote it, and the cycle it runs through. True where there was one.
bool refuse_build_fault(std::string_view grammar, const LlBuiltProgram& a,
                        AsWrittenFn asWritten) {
  if (!a.fault) return false;
  const std::string_view rule = a.cfg.rule_name(a.fault.rule);
  std::vector<std::string_view> cycle;
  for (const auto q : a.faultCycle)
    cycle.push_back(a.cfg.rule_name(static_cast<std::uint16_t>(q)));
  refuse_fault(grammar, a.fault,
               grammar_fault_message(
                   a.fault, rule, asWritten ? asWritten(rule) : std::string{},
                   grammar_fault_cycle(cycle)));
  return true;
}

// The first terminal whose match is empty, empty where there is none.
std::string_view ll_empty_match_terminal(const LlBuiltProgram& a) {
  return empty_match_terminal(a, [&](std::uint16_t state) {
    for (std::uint16_t t = 1; t < a.cfg.nterms; ++t)
      if (a.dfaAccSet[state].test(t)) return static_cast<SymbolId>(t);
    return kInvalidSymbol;
  });
}

}  // namespace

bool ll_fits(std::string_view grammar, std::size_t n, const char* what) {
  if (n <= 0xFFFFu) return true;
  refuse_too_large(grammar,
                   "it has " + std::to_string(n) + ' ' + what +
                       ", more than 65 535");
  return false;
}

std::optional<LlFinalProgram> LlFinalProgram::make(LlBuiltProgram built,
                                                   std::string_view grammar,
                                                   AsWrittenFn asWritten) {
  if (refuse_build_fault(grammar, built, asWritten) ||
      refuse_unbuilt(grammar, built,
                     built.cfg.rule_name(built.cfg.lowerRule)) ||
      // A stamp's position is a node or an operator of a precedence table.
      refuse_unstamped(grammar, built.stamp, built.writes.nchains,
                       built.cfg.nnodes + 1u) ||
      refuse_empty_match(grammar, ll_empty_match_terminal(built)))
    return std::nullopt;
  return LlFinalProgram(
      std::make_unique<const LlBuiltProgram>(std::move(built)));
}

}  // namespace cursiv
