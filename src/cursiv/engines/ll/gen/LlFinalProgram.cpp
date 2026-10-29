#include "cursiv/engines/ll/gen/LlFinalProgram.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

#include "cursiv/engines/shared/gen/GrammarFault.hpp"
#include "cursiv/engines/shared/gen/gen_report.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

namespace {

using AsWrittenFn = std::string (*)(std::string_view);

// The name of rule `r`, empty where the program holds no such rule.
std::string_view rule_name_of(const LlBuiltProgram& a, std::uint16_t r) {
  return r < a.cfg.nrules ? a.cfg.rule_name(r) : std::string_view{};
}

// A shape the descent cannot hold: the sentence names the rule, as its author
// wrote it, and the cycle it runs through. True where there was one.
bool refuse_build_fault(std::string_view grammar, const LlBuiltProgram& a,
                        AsWrittenFn asWritten) {
  if (!a.fault) return false;
  const std::string_view rule = rule_name_of(a, a.fault.rule);
  std::vector<std::string_view> cycle;
  for (const auto q : a.faultCycle)
    cycle.push_back(a.cfg.rule_name(static_cast<std::uint16_t>(q)));
  refuse_fault(grammar, a.fault,
               grammar_fault_message(
                   a.fault, rule, asWritten ? asWritten(rule) : std::string{},
                   grammar_fault_cycle(cycle)));
  return true;
}

// What a record's stamp holds: the chain it names, and the position that read
// it -- a node of the program or an operator of a precedence table. The split
// found is kept on the program. True where none holds.
bool refuse_stamp(std::string_view grammar, LlBuiltProgram& a) {
  return refuse_unstamped(grammar, a.stamp, a.writes.nchains,
                          a.cfg.nnodes + 1u);
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
  std::fprintf(stderr, "%.*s: refused, %zu %s overrun sixteen bits\n",
               static_cast<int>(grammar.size()), grammar.data(), n, what);
  return false;
}

std::optional<LlFinalProgram> LlFinalProgram::make(LlBuiltProgram built,
                                                   std::string_view grammar,
                                                   AsWrittenFn asWritten) {
  if (refuse_build_fault(grammar, built, asWritten) ||
      refuse_unbuilt(grammar, built,
                     rule_name_of(built, built.cfg.lowerRule)) ||
      refuse_stamp(grammar, built) ||
      refuse_empty_match(grammar, ll_empty_match_terminal(built)))
    return std::nullopt;
  return LlFinalProgram(
      std::make_unique<const LlBuiltProgram>(std::move(built)));
}

}  // namespace cursiv
