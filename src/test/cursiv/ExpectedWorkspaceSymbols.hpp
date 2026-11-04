// `workspace/symbol`: the query the index answers, across every language of
// the workspace. It builds no document, so it disposes of nothing.
#pragma once

#include <lsp/types.h>

#include <functional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace cursiv {
struct SharedLspServices;
}  // namespace cursiv

namespace cursiv::test {

struct ExpectedWorkspaceSymbolsList {
  std::string query{};
  std::vector<std::string> expectedSymbols{};
  // Empty prints a symbol's `name`.
  std::function<std::string(const lsp::WorkspaceSymbol&)> symbolToString{};
};

struct ExpectedWorkspaceSymbolsCallback {
  std::string query{};
  std::function<void(std::span<const lsp::WorkspaceSymbol>)> assertion{};
};

using ExpectedWorkspaceSymbols = std::variant<ExpectedWorkspaceSymbolsList,
                                              ExpectedWorkspaceSymbolsCallback>;

void expectWorkspaceSymbols(const SharedLspServices& shared,
                            const ExpectedWorkspaceSymbols& input);

}  // namespace cursiv::test
