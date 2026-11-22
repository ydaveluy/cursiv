#include "cursiv/ExpectedWorkspaceSymbols.hpp"

#include <lsp/types.h>

#include <algorithm>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "cursiv/ExpectFunction.hpp"
#include "cursiv/ExpectationError.hpp"
#include "cursiv/services/SharedLspServices.hpp"
#include "cursiv/symbols/WorkspaceSymbolProvider.hpp"
#include "cursiv/utils/Cancellation.hpp"

namespace cursiv::test {

void expectWorkspaceSymbols(const SharedLspServices& shared,
                            const ExpectedWorkspaceSymbols& input) {
  const std::string& query = std::visit(
      [](const auto& arm) -> const std::string& { return arm.query; }, input);

  if (!shared.workspaceSymbolProvider)
    throw ExpectationError("missing workspace symbol provider");
  const std::vector<lsp::WorkspaceSymbol> symbols =
      shared.workspaceSymbolProvider->getSymbols(query, kNoCancel);

  if (const auto* const callback =
          std::get_if<ExpectedWorkspaceSymbolsCallback>(&input)) {
    if (callback->assertion) callback->assertion(symbols);
    return;
  }

  const auto& expected = std::get<ExpectedWorkspaceSymbolsList>(input);
  std::vector<std::string> actual;
  actual.reserve(symbols.size());
  for (const lsp::WorkspaceSymbol& symbol : symbols)
    actual.push_back(expected.symbolToString ? expected.symbolToString(symbol)
                                             : symbol.name);

  auto missing = expected.kinds;
  for (const auto& symbol : symbols) {
    if (missing.empty()) break;
    if (const auto at = missing.find(symbol.name); at != missing.end()) {
      expectedFunction(symbol.kind == at->second ? "match" : "wrong kind",
                       "match", symbol.name);
      missing.erase(at);
    }
  }
  for (const auto& [name, kind] : missing)
    expectedFunction("missing", "present", name);
  if (expected.match != SymbolMatch::Ordered) {
    std::set<std::string_view> names(actual.begin(), actual.end());
    for (const auto& name : expected.expectedSymbols) {
      expectedFunction(names.contains(name) ? "present" : "missing", "present",
                       name);
      if (expected.match == SymbolMatch::ExactSet) names.erase(name);
    }
    if (expected.match == SymbolMatch::ExactSet)
      expectedFunction(std::to_string(names.size()), "0",
                       "unexpected workspace symbols");
    return;
  }
  detail::expectedItems(actual, expected.expectedSymbols,
                        "expected " +
                            std::to_string(expected.expectedSymbols.size()) +
                            " but found " + std::to_string(symbols.size()) +
                            " symbols in workspace");
}

}  // namespace cursiv::test
