#include "cursiv/ll/gen/LlFinalProgram.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include "cursiv/shared/gen/GrammarFault.hpp"
#include "cursiv/shared/gen/gen_report.hpp"
#include "cursiv/shared/table/Token.hpp"

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

}  // namespace

std::optional<LlFinalProgram> LlFinalProgram::make(LlBuiltProgram built,
                                                   std::string_view grammar,
                                                   AsWrittenFn asWritten) {
  if (refuse_build_fault(grammar, built, asWritten) ||
      refuse_unbuilt(grammar, built,
                     built.cfg.rule_name(built.cfg.lowerRule)) ||
      // A stamp's position is a node, one past its index.
      refuse_unstamped(grammar, built.stamp, built.writes.nchains,
                       built.cfg.nnodes + 1u) ||
      refuse_empty_match(grammar, empty_match_terminal(built)))
    return std::nullopt;
  return LlFinalProgram(
      std::make_unique<const LlBuiltProgram>(std::move(built)));
}

}  // namespace cursiv
