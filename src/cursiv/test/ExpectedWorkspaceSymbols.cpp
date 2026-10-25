#include "cursiv/test/ExpectedWorkspaceSymbols.hpp"

#include <lsp/types.h>

#include <cstddef>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/lsp/services/SharedLspServices.hpp"
#include "cursiv/lsp/symbols/WorkspaceSymbolProvider.hpp"
#include "cursiv/test/ExpectFunction.hpp"

namespace cursiv::test {

namespace {

std::string joined(std::span<const std::string> names) {
  std::string out;
  for (const std::string& name : names) {
    if (!out.empty()) out += ", ";
    out += name;
  }
  return out;
}

}  // namespace

void expectWorkspaceSymbols(const SharedLspServices& shared,
                            const ExpectedWorkspaceSymbols& input) {
  const std::string& query = std::visit(
      [](const auto& arm) -> const std::string& { return arm.query; }, input);

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

  if (actual.size() == expected.expectedSymbols.size())
    for (std::size_t at = 0; at < actual.size(); ++at)
      expectedFunction(actual[at], expected.expectedSymbols[at]);
  else
    expectedFunction(joined(actual), joined(expected.expectedSymbols),
                     "expected " +
                         std::to_string(expected.expectedSymbols.size()) +
                         " but found " + std::to_string(symbols.size()) +
                         " symbols in workspace");
}

}  // namespace cursiv::test
