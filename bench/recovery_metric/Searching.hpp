// The generated language under the metric's budget, in either engine. Use
// the runtime parser itself so the metric follows the language module's path.
#pragma once

#include <memory>
#include <string_view>
#include <type_traits>

#include "cursiv/engines/ll/runtime/LlLanguageParser.hpp"
#include "cursiv/engines/lr/runtime/LrLanguageParser.hpp"
#include "cursiv/engines/shared/runtime/RepairBudget.hpp"
#include "recovery_metric/MetricLanguage.hpp"

namespace recovery_metric {

template <const auto& Table, const auto& Drive,
          const cursiv::GrammarMetaData& Grammar,
          const cursiv::AstActions& Actions>
std::unique_ptr<cursiv::Parser> searching(const cursiv::RepairBudget& budget) {
  using Engine = std::conditional_t<
      std::is_pointer_v<std::remove_cvref_t<decltype(Drive)>>,
      cursiv::LlLanguageParser<Table>, cursiv::LrLanguageParser<Table>>;
  return std::make_unique<Engine>(Grammar, Actions, Drive, budget);
}

template <const auto& Table, const auto& Drive,
          const cursiv::GrammarMetaData& Grammar,
          const cursiv::AstActions& Actions>
Ceilings ceilings(std::string_view text, const cursiv::RepairBudget& budget) {
  Ceilings counts{};
  if constexpr (std::is_pointer_v<std::remove_cvref_t<decltype(Drive)>>) {
    const auto result =
        searching<Table, Drive, Grammar, Actions>(budget)->parse(text);
    // The predictive engine reports its work ceiling on the diagnostics;
    // the other counters describe the table engine's speculative machinery.
    for (const auto& d : result.diagnostics)
      counts[2] += d.kind == cursiv::DiagnosticKind::bounded;
  } else {
    cursiv::Cst cst;
    cursiv::LrParseStats stats{};
    (void)cursiv::lr_parser<Table>.parse(text, cst, &stats, &Drive, budget);
    for (const auto& d : cst.diagnostics())
      counts[2] += d.kind == cursiv::DiagnosticKind::bounded;
    counts[0] = stats.searchStops;
    counts[1] = stats.lookStops;
    counts[3] = stats.reachStops;
    counts[4] = stats.budgetStops;
    counts[5] = stats.degradedPauses;
    counts[6] = stats.planDrops;
    counts[7] = stats.episodeSearches;
  }
  return counts;
}

}  // namespace recovery_metric
