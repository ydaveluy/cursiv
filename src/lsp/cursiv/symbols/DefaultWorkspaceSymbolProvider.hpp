// Backed by the index, filtered by the fuzzy matcher, iconed by the node-kind
// provider -- the three services this one composes rather than reimplements.
#pragma once

#include <lsp/types.h>

#include <optional>
#include <string_view>
#include <vector>

#include "cursiv/workspace/AstNodeDescription.hpp"
#include "cursiv/services/SharedLspServices.hpp"
#include "cursiv/symbols/WorkspaceSymbolProvider.hpp"

namespace cursiv {

class DefaultWorkspaceSymbolProvider : public WorkspaceSymbolProvider {
 public:
  explicit DefaultWorkspaceSymbolProvider(SharedLspServices& shared) noexcept
      : _shared(shared) {}

  [[nodiscard]] std::vector<lsp::WorkspaceSymbol> getSymbols(
      std::string_view query, const CancellationToken& = {}) const override;

 protected:
  // One symbol from one index row, or none to drop the row: a language
  // reshapes a symbol here without rewriting the walk.
  [[nodiscard]] virtual std::optional<lsp::WorkspaceSymbol> getWorkspaceSymbol(
      const AstNodeDescription& astDescription) const;

 private:
  SharedLspServices& _shared;
};

}  // namespace cursiv
