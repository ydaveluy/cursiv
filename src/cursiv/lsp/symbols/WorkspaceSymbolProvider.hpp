// Every declaration of the workspace that answers a query, across
// languages. The one navigation door that is not a document's: it reads
// the index, so a symbol of a file nobody opened is reachable.
#pragma once

#include <lsp/types.h>

#include <string_view>
#include <vector>

#include "cursiv/core/utils/Cancellation.hpp"

namespace cursiv {

class WorkspaceSymbolProvider {
 public:
  virtual ~WorkspaceSymbolProvider() noexcept = default;

  /**
   * Finds the workspace symbols matching `query`. Precondition: called
   * under the workspace read lock.
   */
  [[nodiscard]] virtual std::vector<lsp::WorkspaceSymbol> getSymbols(
      std::string_view query, const CancellationToken& = {}) const = 0;

  // Whether `resolveSymbol` is overridden, which `resolveProvider` announces:
  // an override cannot be told from the default it replaces.
  [[nodiscard]] virtual bool resolvesSymbols() const noexcept { return false; }

  /**
   * Resolves `symbol`. Precondition: called under the workspace read lock.
   */
  [[nodiscard]] virtual lsp::WorkspaceSymbol resolveSymbol(
      lsp::WorkspaceSymbol symbol, const CancellationToken& = {}) const {
    return symbol;
  }
};

}  // namespace cursiv
