// A document's outline, as the protocol's `lsp::DocumentSymbol` tree. It
// takes the document alone, so offsets and their index always match.
#pragma once

#include <lsp/types.h>

#include <vector>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"

namespace cursiv {

class DocumentSymbolProvider {
 public:
  virtual ~DocumentSymbolProvider() noexcept = default;

  // The outline of `document`, under the workspace read lock.
  [[nodiscard]] virtual std::vector<lsp::DocumentSymbol> getSymbols(
      const Document& document, const CancellationToken& = {}) const = 0;
};

}  // namespace cursiv
