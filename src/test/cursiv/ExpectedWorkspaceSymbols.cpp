#include "cursiv/ExpectedWorkspaceSymbols.hpp"

#include <lsp/types.h>

#include <string>
#include <variant>
#include <vector>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/services/SharedLspServices.hpp"
#include "cursiv/symbols/WorkspaceSymbolProvider.hpp"
#include "cursiv/ExpectFunction.hpp"

namespace cursiv::test {

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

  detail::expectedItems(actual, expected.expectedSymbols,
                        "expected " +
                            std::to_string(expected.expectedSymbols.size()) +
                            " but found " + std::to_string(symbols.size()) +
                            " symbols in workspace");
}

}  // namespace cursiv::test
